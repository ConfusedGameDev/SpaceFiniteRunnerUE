#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CollisionQueryParams.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PhysicsEngine/BodySetup.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Track/Features/JumpRamp.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/Spawning/TrackSpawner.h"
#include "Track/TrackColliderBuilder.h"
#include "Track/TrackDecider.h"
#include "Track/TrackDecorator.h"
#include "Track/TrackGenerator.h"
#include "Track/TrackLayoutAsset.h"
#include "Track/TrackModel.h"
#include "Track/TrackPieces.h"
#include "Track/TrackSection.h"
#include "Track/TrackShape.h"

namespace
{
	constexpr EAutomationTestFlags BuildTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UTrackShape* LoadShape() { return LoadObject<UTrackShape>(nullptr, TEXT("/Game/Runner/Data/Track/DA_TrackShape.DA_TrackShape")); }
	UTrackSpawnSet* LoadSpawnSet() { return LoadObject<UTrackSpawnSet>(nullptr, TEXT("/Game/Runner/Data/Track/Spawning/DA_TrackSpawnSet.DA_TrackSpawnSet")); }
	UJumpDefinition* LoadEndRamp() { return LoadObject<UJumpDefinition>(nullptr, TEXT("/Game/Runner/Data/Track/Features/DA_EndRamp.DA_EndRamp")); }

	/** A bare game world for one test; torn down with it. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RunnerTrackBuildTest"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void DescribeFirstChunk(FAutomationTestBase& Test, ATrackGenerator& Generator)
	{
		TArray<UDynamicMeshComponent*> Meshes;
		Generator.GetComponents(Meshes);
		if (Meshes.Num() == 0)
		{
			Test.AddError(TEXT("no collision chunk components"));
			return;
		}
		UDynamicMeshComponent* First = Meshes[0];
		UBodySetup* Setup = First->GetBodySetup();
		Test.AddInfo(FString::Printf(TEXT("%d chunks; first: %d tris, registered %d, body %d, setup %d, trimeshes %d, collision %d, bounds %s"),
			Meshes.Num(), First->GetMesh()->TriangleCount(), First->IsRegistered(), First->BodyInstance.IsValidBodyInstance(),
			Setup != nullptr, Setup ? Setup->TriMeshGeometries.Num() : -1, static_cast<int32>(First->GetCollisionEnabled()), *First->Bounds.GetBox().ToString()));
		const FVector C = First->Bounds.Origin;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RunnerTrackBuildProbe), true);
		const bool bComponent = First->LineTraceComponent(Hit, C + FVector(0, 0, 5000), C - FVector(0, 0, 5000), Params);
		FHitResult WorldHit;
		const bool bWorld = First->GetWorld()->LineTraceSingleByChannel(WorldHit, C + FVector(0, 0, 5000), C - FVector(0, 0, 5000), ECC_WorldStatic, Params);
		const bool bAny = First->GetWorld()->LineTraceSingleByObjectType(WorldHit, C + FVector(0, 0, 5000), C - FVector(0, 0, 5000), FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Params);
		Test.AddInfo(FString::Printf(TEXT("probe at %s: component %d (%s), world channel %d, world any %d, response %d, object type %d, scene %d, body in scene %d"),
			*C.ToString(), bComponent, *Hit.ImpactPoint.ToString(), bWorld, bAny, static_cast<int32>(First->GetCollisionResponseToChannel(ECC_WorldStatic)),
			static_cast<int32>(First->GetCollisionObjectType()), First->GetWorld()->GetPhysicsScene() != nullptr, First->BodyInstance.GetPhysicsScene() != nullptr));
	}

	bool Trace(UWorld& World, const FVector& From, const FVector& To, FHitResult& Hit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RunnerTrackBuildTest), true);
		return World.LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, Params);
	}

	/**
	 * Flies the focus from the start to past the end in steps and, inside every step's window,
	 * checks what the ship would meet:
	 * - the floor under three laterals across the lane every 5 m, where the flight line is (ramps aside);
	 * - a road piece at every stamp distance between the cull line and the window's edge;
	 * - a wall at the lip where the edge is closed and none where it is open (plain road, every 20 m);
	 * - nothing at all past the end.
	 */
	void FlyThrough(FAutomationTestBase& Test, UWorld& World, ATrackGenerator& Generator, const FString& Label)
	{
		UTrackModel& Track = *Generator.GetTrack();
		const FTransform& ToWorld = Track.GetTrackToWorld();
		const float End = Track.HasEnd() ? Track.GetEndDistance() : Track.GetLength();
		const float Spacing = Generator.Decorator->RoadSpacing;
		constexpr float Step = 50.f;
		int32 FloorMisses = 0, FloorOff = 0, RoadGaps = 0, WallMisses = 0, WallExtra = 0, Probes = 0;
		constexpr int32 MaxReports = 8;
		int32 EdgeMisses = 0, TransitionHumps = 0, RoadAbove = 0;
		TMap<int32, int32> MissPhase; // floor misses by distance into a chunk

		auto WorldPoint = [&](float D, float Lateral, float Up, FVector& OutUp)
		{
			FVector P;
			FQuat R;
			Track.GetPoseAtDistance(D, Lateral, P, R);
			OutUp = ToWorld.TransformVectorNoScale(R.GetUpVector());
			return Track.ToWorldPosition(P + R.GetUpVector() * Up);
		};

		for (float Focus = 0.f; Focus < End; Focus += Step)
		{
			Generator.StreamTo(Focus);
			// Chaos takes new bodies into its game-thread query structure at the end of a physics frame;
			// a test that never ticks flushes it by hand.
			if (FPhysScene* Scene = World.GetPhysicsScene()) Scene->Flush();
			else if (Focus == 0.f) Test.AddError(TEXT("the test world has no physics scene"));
			if (Focus == 0.f) DescribeFirstChunk(Test, Generator);
			const float To = FMath::Min(Focus + Step, End);

			// Floor.
			for (float D = Focus; D < To; D += 5.f)
			{
				const bool bOnRamp = Generator.RampAt(D) != nullptr;
				float Min, Max;
				Track.GetLateralBand(D, Min, Max);
				for (const float U : { 0.05f, 0.5f, 0.95f })
				{
					const float Lateral = FMath::Lerp(Min, Max, U);
					FVector Up;
					const FVector Line = WorldPoint(D, Lateral, 0.f, Up);
					FHitResult Hit;
					++Probes;
					// From 2 m above: a loop's exit ride can pass a few metres over its own entry.
					const FVector From = Line + Up * 200.0, ToPoint = Line - Up * 1000.0;
					const bool bRay = Trace(World, From, ToPoint, Hit);
					double Off = bRay ? FVector::DotProduct(Hit.ImpactPoint - Line, Up) / 100.0 : 0.0;
					if (!bRay || FMath::Abs(Off) > 0.5)
					{
						// A ray straight down a shared triangle edge can slip between the two in Chaos's
						// ray-triangle test (and hit whatever lies below). A 5 cm sphere tells that from a hole.
						FHitResult SweepHit;
						FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(RunnerTrackBuildSweep), true);
						if (World.SweepSingleByChannel(SweepHit, From, ToPoint, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(5.f), SweepParams))
						{
							const double SweepOff = FVector::DotProduct(SweepHit.ImpactPoint - Line, Up) / 100.0;
							if (!bRay || FMath::Abs(SweepOff) < FMath::Abs(Off))
							{
								++EdgeMisses;
								Hit = SweepHit;
								Off = SweepOff;
							}
						}
						else if (!bRay)
						{
							MissPhase.FindOrAdd(FMath::RoundToInt(FMath::Fmod(D, 120.f)))++;
							if (++FloorMisses <= MaxReports) Test.AddError(FString::Printf(TEXT("%s: no floor at %.1f m, lateral %.1f"), *Label, D, Lateral));
							continue;
						}
					}
					// Where the road gains or loses its shoulders (a loop or tube mouth), a chunk's one grid
					// runs an edge column from the shoulder slope into the lane: a hump of a metre or so at
					// the lane edge, as in Unity's builder.
					const float Row = Generator.Colliders->SampleSpacing;
					const bool bTransition = Track.HasShoulders(D - Row) != Track.HasShoulders(D + Row)
						|| Track.HasShoulders(D - 2.f * Row) != Track.HasShoulders(D + 2.f * Row);
					if (bOnRamp) continue;
					if (bTransition && FMath::Abs(Off) > 0.5 && FMath::Abs(Off) <= 1.5)
					{
						++TransitionHumps;
						continue;
					}
					if (FMath::Abs(Off) > 0.5 && Hit.GetComponent() != Generator.Colliders->GetChunkAt(D))
					{
						// Another stretch of road just over this one (a loop's exit ride crossing its entry).
						// This distance's own floor must still be where the line is.
						FHitResult Own;
						if (Trace(World, Line + Up * 30.0, ToPoint, Own) && Own.GetComponent() == Generator.Colliders->GetChunkAt(D)
							&& FMath::Abs(FVector::DotProduct(Own.ImpactPoint - Line, Up) / 100.0) <= 0.5)
						{
							++RoadAbove;
							continue;
						}
					}
					if (FMath::Abs(Off) > 0.5 && ++FloorOff <= MaxReports)
					{
						const FTrackSection* S = Track.SectionAt(D);
						const FTubeSection* T = S ? S->AsTube() : nullptr;
						const FString Where = !S ? FString(TEXT("road")) : T ? FString::Printf(TEXT("tube, curl %.3f"), T->Curl(D - T->GetStartDistance())) : FString::Printf(TEXT("loop %.0f m in"), D - S->GetStartDistance());
						Test.AddError(FString::Printf(TEXT("%s: floor %.2f m off the flight line at %.1f m, lateral %.1f (%s)"), *Label, Off, D, Lateral, *Where));
					}
				}
			}

			// Walls: plain road only, away from where an edge opens or closes.
			for (float D = FMath::CeilToFloat(Focus / 20.f) * 20.f; D < To - 10.f; D += 20.f)
			{
				if (Track.SectionAt(D) || !Track.HasShoulders(D) || Generator.RampAt(D)) continue;
				// A chunk that holds any of a loop carries no walls at all (as in Unity).
				const float Chunk = Generator.Colliders->ChunkLength;
				const bool bNearLoop = Track.GetSections().ContainsByPredicate([&](const TSharedPtr<FTrackSection>& S)
				{
					return S->AsLoop() && S->GetStartDistance() < D + Chunk && S->GetEndDistance() > D - Chunk;
				});
				if (bNearLoop) continue;
				float RoadMin, RoadMax;
				Track.GetRoadBand(D, RoadMin, RoadMax);
				const float Lift = Track.GetShoulderRise() + 3.f;
				for (const int32 Side : { -1, 1 })
				{
					if (Track.IsEdgeOpen(D - 8.f, Side) != Track.IsEdgeOpen(D + 8.f, Side)) continue;
					const float Lip = Side < 0 ? RoadMin : RoadMax;
					FVector Up;
					const FVector Inside = WorldPoint(D, Lip - Side * 1.f, Lift, Up);
					const FVector Outside = WorldPoint(D, Lip + Side * 3.f, Lift, Up);
					FHitResult Hit;
					const bool bHit = Trace(World, Inside, Outside, Hit);
					const bool bOpen = Track.IsEdgeOpen(D, Side);
					if (!bOpen && !bHit && ++WallMisses <= MaxReports)
						Test.AddError(FString::Printf(TEXT("%s: no wall on side %d at %.1f m"), *Label, Side, D));
					if (bOpen && bHit && ++WallExtra <= MaxReports)
						Test.AddError(FString::Printf(TEXT("%s: a wall on the open side %d at %.1f m"), *Label, Side, D));
				}
			}

			// Road pieces: one stamp every spacing between the cull line and the edge.
			TArray<TPair<float, const USplineMeshComponent*>> Stamps;
			Generator.Decorator->GetLiveStamps(Stamps);
			TSet<int32> Keys;
			for (const TPair<float, const USplineMeshComponent*>& S : Stamps)
			{
				if (S.Value && S.Value->IsVisible()) Keys.Add(FMath::RoundToInt(S.Key * 10.f));
			}
			const float From = FMath::Max(Generator.GetCulledTo(), 0.f);
			const int32 FirstK = FMath::Max(0, FMath::CeilToInt((From - Spacing * 0.5f) / Spacing));
			for (int32 K = FirstK;; ++K)
			{
				const float D = Spacing * 0.5f + K * Spacing;
				if (D >= Generator.GetBuiltTo() || D >= End) break;
				if (!Keys.Contains(FMath::RoundToInt(D * 10.f)) && ++RoadGaps <= MaxReports)
					Test.AddError(FString::Printf(TEXT("%s: no road piece stamped at %.1f m"), *Label, D));
			}
		}

		// The void past the end.
		Generator.StreamTo(End);
		if (FPhysScene* Scene = World.GetPhysicsScene()) Scene->Flush();
		for (const float Past : { 30.f, 120.f })
		{
			FVector Up;
			const FVector Line = WorldPoint(End + Past, 0.f, 0.f, Up);
			FHitResult Hit;
			Test.TestFalse(FString::Printf(TEXT("%s: nothing %.0f m past the end"), *Label, Past), Trace(World, Line + Up * 500.0, Line - Up * 3000.0, Hit));
		}

		for (const TPair<int32, int32>& M : MissPhase) Test.AddInfo(FString::Printf(TEXT("%s: %d misses at %d m into a chunk"), *Label, M.Value, M.Key));
		Test.TestEqual(FString::Printf(TEXT("%s: floor misses (of %d probes)"), *Label, Probes), FloorMisses, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: floor off the flight line"), *Label), FloorOff, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: road gaps"), *Label), RoadGaps, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: missing walls"), *Label), WallMisses, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: walls on open edges"), *Label), WallExtra, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: ramps were built"), *Label), Generator.Colliders->GetRampCount() > 0 || Generator.GetRamps().Num() > 0);
		Test.AddInfo(FString::Printf(TEXT("%s: %.0f m streamed, %d floor probes (%d rays down a triangle edge, caught by the sphere; %d humps at a shoulder transition; %d under another stretch of road), %d chunks live at the end"), *Label, End, Probes, EdgeMisses, TransitionHumps, RoadAbove, Generator.Colliders->GetChunkCount()));
	}
}

// ------------------------------------------------------------- piece pose

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackPiecePoseTest, "Runner.Track.Build.PiecePose", BuildTestFlags)

bool FRunnerTrackPiecePoseTest::RunTest(const FString& Parameters)
{
	// Both ends of a piece sit on the track pose there, with the mesh's up on the track's up and its
	// across axis on the track's right, for both forward axes, on banked and sloped road.
	UTrackDecider* Decider = NewObject<UTrackDecider>();
	FTrackDecideParams Params;
	Params.Seed = 4242;
	Decider->Generate(LoadShape(), LoadSpawnSet(), LoadEndRamp(), Params);
	const UTrackModel* Track = Decider->GetTrack();
	if (!TestNotNull(TEXT("track"), Track)) return false;

	USplineMeshComponent* Piece = NewObject<USplineMeshComponent>();
	int32 Banked = 0;
	for (float D = 500.f; D < Track->GetLength() - 100.f; D += 397.f)
	{
		if (FMath::Abs(Track->GetBankAtDistance(D)) > 5.f) ++Banked;
		for (const ESplineMeshAxis::Type Axis : { ESplineMeshAxis::X, ESplineMeshAxis::Y })
		{
			FTrackPieceFit Fit;
			Fit.ForwardAxis = Axis;
			Fit.UpOffset = 1.5f;
			const float Lateral = 20.f;
			TrackPieces::Pose(*Piece, *Track, D, D + 19.f, Lateral, Fit);
			for (const float Alpha : { 0.f, 1.f })
			{
				FVector P;
				FQuat R;
				Track->GetPoseAtDistance(D + Alpha * 19.f, Lateral, P, R);
				const FTransform Slice = Piece->CalcSliceTransformAtSplineOffset(Alpha);
				const FVector Expected = (P + R.GetUpVector() * Fit.UpOffset) * 100.0;
				const FVector MeshUp = Slice.GetUnitAxis(EAxis::Z);
				const FVector MeshAcross = Slice.GetUnitAxis(Axis == ESplineMeshAxis::X ? EAxis::Y : EAxis::X);
				const FString Where = FString::Printf(TEXT("%s at %.0f m end %.0f"), Axis == ESplineMeshAxis::X ? TEXT("X") : TEXT("Y"), D, Alpha);
				TestTrue(FString::Printf(TEXT("%s: position (%.2f cm off)"), *Where, FVector::Distance(Slice.GetLocation(), Expected)), FVector::Distance(Slice.GetLocation(), Expected) < 1.0);
				TestTrue(FString::Printf(TEXT("%s: up (dot %.5f)"), *Where, MeshUp | R.GetUpVector()), (MeshUp | R.GetUpVector()) > 0.9999);
				TestTrue(FString::Printf(TEXT("%s: across (dot %.5f)"), *Where, MeshAcross | R.GetRightVector()), FMath::Abs(MeshAcross | R.GetRightVector()) > 0.9999);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d banked samples"), Banked));
	return true;
}

// ----------------------------------------------------------------- stream

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackStreamGeneratedTest, "Runner.Track.Build.StreamGenerated", BuildTestFlags)

bool FRunnerTrackStreamGeneratedTest::RunTest(const FString& Parameters)
{
	FTestWorld TestWorld;
	FActorSpawnParameters SpawnParams;
	SpawnParams.bDeferConstruction = true;
	ATrackGenerator* Generator = TestWorld.World->SpawnActor<ATrackGenerator>(ATrackGenerator::StaticClass(), FTransform(FVector(1000.0, -500.0, 200.0)), SpawnParams);
	if (!TestNotNull(TEXT("generator"), Generator)) return false;
	Generator->bBuildOnBeginPlay = false;
	Generator->Shape = LoadShape();
	Generator->SpawnSet = LoadSpawnSet();
	Generator->EndRamp = LoadEndRamp();
	Generator->Seed = 4242;
	Generator->FinishSpawning(FTransform(FVector(1000.0, -500.0, 200.0)));
	if (!TestTrue(TEXT("track built"), Generator->BuildTrack())) return false;
	FlyThrough(*this, *TestWorld.World, *Generator, TEXT("seed 4242"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackStreamStandardTest, "Runner.Track.Build.StreamStandard", BuildTestFlags)

bool FRunnerTrackStreamStandardTest::RunTest(const FString& Parameters)
{
	FTestWorld TestWorld;
	FActorSpawnParameters SpawnParams;
	SpawnParams.bDeferConstruction = true;
	ATrackGenerator* Generator = TestWorld.World->SpawnActor<ATrackGenerator>(ATrackGenerator::StaticClass(), FTransform::Identity, SpawnParams);
	if (!TestNotNull(TEXT("generator"), Generator)) return false;
	Generator->bBuildOnBeginPlay = false;
	Generator->Layout = LoadObject<UTrackLayoutAsset>(nullptr, TEXT("/Game/Runner/Data/Tracks/TL_Standard.TL_Standard"));
	Generator->SpawnSet = LoadSpawnSet();
	Generator->EndRamp = LoadEndRamp();
	Generator->FinishSpawning(FTransform::Identity);
	if (!TestNotNull(TEXT("TL_Standard"), Generator->Layout.Get())) return false;
	if (!TestTrue(TEXT("track loaded"), Generator->BuildTrack())) return false;
	FlyThrough(*this, *TestWorld.World, *Generator, TEXT("TL_Standard"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackStreamMixedTest, "Runner.Track.Build.StreamMixed", BuildTestFlags)

bool FRunnerTrackStreamMixedTest::RunTest(const FString& Parameters)
{
	// The shipped shape is all jumps: this one rolls every feature kind its table holds evenly, so
	// loops and tubes stream too.
	UTrackShape* Mixed = DuplicateObject<UTrackShape>(LoadShape(), GetTransientPackage());
	if (!TestNotNull(TEXT("shape"), Mixed)) return false;
	for (FTrackFeatureEntry& Entry : Mixed->FeatureTable) Entry.Probability = 100.f / FMath::Max(1, Mixed->FeatureTable.Num());

	for (const int32 Seed : { 7, 1234, 98765 })
	{
		FTestWorld TestWorld;
		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		ATrackGenerator* Generator = TestWorld.World->SpawnActor<ATrackGenerator>(ATrackGenerator::StaticClass(), FTransform::Identity, SpawnParams);
		Generator->bBuildOnBeginPlay = false;
		Generator->Shape = Mixed;
		Generator->SpawnSet = LoadSpawnSet();
		Generator->EndRamp = LoadEndRamp();
		Generator->Seed = Seed;
		Generator->FinishSpawning(FTransform::Identity);
		if (!TestTrue(TEXT("track built"), Generator->BuildTrack())) continue;
		int32 Loops = 0, Tubes = 0;
		for (const TSharedPtr<FTrackSection>& S : Generator->GetTrack()->GetSections())
		{
			Loops += S->AsLoop() != nullptr;
			Tubes += S->AsTube() != nullptr;
		}
		AddInfo(FString::Printf(TEXT("seed %d: %d loops, %d tubes"), Seed, Loops, Tubes));
		FlyThrough(*this, *TestWorld.World, *Generator, FString::Printf(TEXT("mixed seed %d"), Seed));
	}
	return true;
}

#endif
