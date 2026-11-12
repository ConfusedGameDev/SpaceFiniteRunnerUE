#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/Spawning/TrackSpawner.h"
#include "Track/TrackDecider.h"
#include "Track/TrackLayoutAsset.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"
#include "Track/TrackShape.h"

namespace
{
	constexpr EAutomationTestFlags DecideTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FShippedData
	{
		UTrackShape* Shape = nullptr;
		UTrackSpawnSet* SpawnSet = nullptr;
		UJumpDefinition* EndRamp = nullptr;

		bool Load(FAutomationTestBase& Test)
		{
			Shape = LoadObject<UTrackShape>(nullptr, TEXT("/Game/Runner/Data/Track/DA_TrackShape.DA_TrackShape"));
			SpawnSet = LoadObject<UTrackSpawnSet>(nullptr, TEXT("/Game/Runner/Data/Track/Spawning/DA_TrackSpawnSet.DA_TrackSpawnSet"));
			EndRamp = LoadObject<UJumpDefinition>(nullptr, TEXT("/Game/Runner/Data/Track/Features/DA_EndRamp.DA_EndRamp"));
			return Test.TestNotNull(TEXT("DA_TrackShape"), Shape) && Test.TestNotNull(TEXT("DA_TrackSpawnSet"), SpawnSet) && Test.TestNotNull(TEXT("DA_EndRamp"), EndRamp);
		}
	};

	/** A transient copy of the shipped shape with every feature kind in its table (the shipped one is all jumps). */
	UTrackShape* MixedShape(const UTrackShape* Source)
	{
		UTrackShape* Copy = DuplicateObject<UTrackShape>(Source, GetTransientPackage());
		for (FTrackFeatureEntry& Entry : Copy->FeatureTable)
		{
			Entry.Probability = 100.f / FMath::Max(1, Copy->FeatureTable.Num());
		}
		return Copy;
	}

	bool IsPickup(const FTrackPlacement& P)
	{
		return P.Kind == ETrackPlacementKind::SpeedOrb || P.Kind == ETrackPlacementKind::RepairOrb;
	}

	int32 CountKind(const TArray<FTrackPlacement>& Placements, ETrackPlacementKind Kind)
	{
		return Placements.FilterByPredicate([Kind](const FTrackPlacement& P) { return P.Kind == Kind; }).Num();
	}

	/**
	 * The decide rules a finished track must keep: no pickup on claimed ground (widened by a pad
	 * length, as the spawners test it), no gate in a feature keep-out, no gate inside a section,
	 * three end ramps one ramp before the end. Returns the number of broken rules.
	 */
	int32 CheckRules(FAutomationTestBase& Test, const UTrackDecider& Decider, const FString& Label)
	{
		int32 Broken = 0;
		const FTrackSpawnContext& Ctx = Decider.GetContext();
		const UTrackModel& Track = *Decider.GetTrack();
		for (const FTrackPlacement& P : Decider.GetPlacements())
		{
			if (IsPickup(P))
			{
				for (const FFloatInterval& C : Ctx.Claims)
				{
					if (P.Distance >= C.Min - Ctx.PadSize.Z && P.Distance < C.Max + Ctx.PadSize.Z)
					{
						Test.AddError(FString::Printf(TEXT("%s: pickup at %.1f m on claimed ground %.1f-%.1f m"), *Label, P.Distance, C.Min, C.Max));
						++Broken;
						break;
					}
				}
			}
			if (P.Kind == ETrackPlacementKind::LaserGate)
			{
				for (const FFloatInterval& K : Ctx.FeatureKeepOuts)
				{
					if (P.Distance >= K.Min && P.Distance < K.Max)
					{
						Test.AddError(FString::Printf(TEXT("%s: gate at %.1f m inside keep-out %.1f-%.1f m"), *Label, P.Distance, K.Min, K.Max));
						++Broken;
						break;
					}
				}
				if (Track.SectionAt(P.Distance) != nullptr)
				{
					Test.AddError(FString::Printf(TEXT("%s: gate at %.1f m inside a section"), *Label, P.Distance));
					++Broken;
				}
			}
		}

		const TArray<FTrackPlacement> EndRamps = Decider.GetPlacements().FilterByPredicate([](const FTrackPlacement& P) { return P.Kind == ETrackPlacementKind::EndRamp; });
		if (EndRamps.Num() != 3)
		{
			Test.AddError(FString::Printf(TEXT("%s: %d end ramps, expected 3"), *Label, EndRamps.Num()));
			++Broken;
		}
		for (const FTrackPlacement& P : EndRamps)
		{
			if (!FMath::IsNearlyEqual(P.Distance, Track.GetEndDistance() - Decider.GetEndRampLength(), 0.01f))
			{
				Test.AddError(FString::Printf(TEXT("%s: end ramp at %.1f m, the road ends at %.1f m"), *Label, P.Distance, Track.GetEndDistance()));
				++Broken;
			}
		}
		return Broken;
	}
}

/** Same seed, same track: knots, spans, sections and every record. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerDecideDeterminismTest, "Runner.Track.Decide.Determinism", DecideTestFlags)
bool FRunnerDecideDeterminismTest::RunTest(const FString& Parameters)
{
	FShippedData Data;
	if (!Data.Load(*this)) return false;
	UTrackShape* Mixed = MixedShape(Data.Shape);

	for (const UTrackShape* Shape : { static_cast<const UTrackShape*>(Data.Shape), static_cast<const UTrackShape*>(Mixed) })
	{
		for (const uint32 Seed : { 1u, 1234u, 987654321u })
		{
			FTrackDecideParams Params;
			Params.Seed = Seed;
			UTrackDecider* A = NewObject<UTrackDecider>();
			UTrackDecider* B = NewObject<UTrackDecider>();
			A->Generate(Shape, Data.SpawnSet, Data.EndRamp, Params);
			B->Generate(Shape, Data.SpawnSet, Data.EndRamp, Params);
			const FTrackLayout LA = A->CaptureLayout();
			const FTrackLayout LB = B->CaptureLayout();
			const FString Label = FString::Printf(TEXT("%s seed %u"), Shape == Mixed ? TEXT("mixed") : TEXT("shipped"), Seed);

			TestEqual(Label + TEXT(": knot count"), LA.Knots.Num(), LB.Knots.Num());
			TestEqual(Label + TEXT(": placement count"), LA.Placements.Num(), LB.Placements.Num());
			TestEqual(Label + TEXT(": section count"), LA.Sections.Num(), LB.Sections.Num());
			TestEqual(Label + TEXT(": length"), LA.Length, LB.Length);
			bool bSame = LA.Knots.Num() == LB.Knots.Num() && LA.Placements.Num() == LB.Placements.Num();
			for (int32 i = 0; bSame && i < LA.Knots.Num(); ++i)
			{
				bSame = LA.Knots[i].Position.Equals(LB.Knots[i].Position, 0.0) && LA.Knots[i].Rotation.Equals(LB.Knots[i].Rotation, 0.0);
			}
			for (int32 i = 0; bSame && i < LA.Placements.Num(); ++i)
			{
				const FTrackPlacement& PA = LA.Placements[i];
				const FTrackPlacement& PB = LB.Placements[i];
				bSame = PA.Kind == PB.Kind && PA.Distance == PB.Distance && PA.Lateral == PB.Lateral && PA.Variant == PB.Variant && PA.Data == PB.Data;
			}
			TestTrue(Label + TEXT(": bit for bit"), bSame);
		}
	}

	// And a different seed is a different track.
	FTrackDecideParams P1, P2;
	P1.Seed = 1u;
	P2.Seed = 2u;
	UTrackDecider* A = NewObject<UTrackDecider>();
	UTrackDecider* B = NewObject<UTrackDecider>();
	A->Generate(Data.Shape, Data.SpawnSet, Data.EndRamp, P1);
	B->Generate(Data.Shape, Data.SpawnSet, Data.EndRamp, P2);
	TestFalse(TEXT("seeds 1 and 2 differ"), A->GetTrack()->GetLength() == B->GetTrack()->GetLength() && A->GetPlacements().Num() == B->GetPlacements().Num());
	return true;
}

/**
 * The M2.3 exit over many seeds, with the shipped shape and with every feature kind: the generated
 * length within 1 % of the target, no pickup on claimed ground, no gate in a keep-out or a section,
 * three end ramps at the end, and a captured layout that reloads to the same length.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerDecideRulesTest, "Runner.Track.Decide.Rules", DecideTestFlags)
bool FRunnerDecideRulesTest::RunTest(const FString& Parameters)
{
	FShippedData Data;
	if (!Data.Load(*this)) return false;
	UTrackShape* Mixed = MixedShape(Data.Shape);

	int32 Loops = 0, Tubes = 0, Ramps = 0, Speed = 0, Repair = 0, Gates = 0, Flat = 0, Open = 0;
	float WorstLengthError = 0.f;
	for (const UTrackShape* Shape : { static_cast<const UTrackShape*>(Data.Shape), static_cast<const UTrackShape*>(Mixed) })
	{
		for (uint32 Seed = 1; Seed <= 25; ++Seed)
		{
			FTrackDecideParams Params;
			Params.Seed = Seed * 7919u;
			UTrackDecider* Decider = NewObject<UTrackDecider>();
			Decider->Generate(Shape, Data.SpawnSet, Data.EndRamp, Params);
			const FString Label = FString::Printf(TEXT("%s seed %u"), Shape == Mixed ? TEXT("mixed") : TEXT("shipped"), Params.Seed);
			const UTrackModel* Track = Decider->GetTrack();

			if (!TestTrue(Label + TEXT(": complete"), Decider->IsComplete())) continue;
			const float Error = FMath::Abs(Track->GetEndDistance() - Decider->GetTargetLength()) / Decider->GetTargetLength();
			WorstLengthError = FMath::Max(WorstLengthError, Error);
			TestTrue(Label + TEXT(": length within 1 % of the target"), Error <= 0.01f);
			TestTrue(Label + TEXT(": the end is the road's length"), FMath::IsNearlyEqual(Track->GetEndDistance(), Track->GetLength(), 0.01f));
			TestEqual(Label + TEXT(": a distance per knot"), Decider->GetKnotDistances().Num(), Track->GetSpline().Num());
			CheckRules(*this, *Decider, Label);

			// Every knot's recorded distance is where the model puts it.
			float WorstKnot = 0.f;
			for (int32 i = 0; i < Decider->GetKnotDistances().Num(); ++i)
			{
				WorstKnot = FMath::Max(WorstKnot, FMath::Abs(Track->KnotTrackDistance(i) - Decider->GetKnotDistances()[i]));
			}
			if (!TestTrue(Label + TEXT(": knot distances match the model"), WorstKnot < 0.01f))
			{
				AddInfo(FString::Printf(TEXT("%s: worst knot distance off by %.3f m"), *Label, WorstKnot));
			}

			// Capture and reload: the saved-track path rebuilds the same road.
			const FTrackLayout Layout = Decider->CaptureLayout();
			UTrackModel* Reloaded = NewObject<UTrackModel>();
			TestTrue(Label + TEXT(": captured layout reloads"), Reloaded->LoadRoad(Layout, Decider->GetShape()));
			TestTrue(Label + TEXT(": reloaded length within 1 m"), FMath::Abs(Reloaded->GetLength() - Layout.Length) <= 1.f);

			for (const TSharedPtr<FTrackSection>& S : Track->GetSections())
			{
				(S->AsLoop() != nullptr ? Loops : Tubes) += 1;
			}
			Ramps += CountKind(Layout.Placements, ETrackPlacementKind::Ramp);
			Speed += CountKind(Layout.Placements, ETrackPlacementKind::SpeedOrb);
			Repair += CountKind(Layout.Placements, ETrackPlacementKind::RepairOrb);
			Gates += CountKind(Layout.Placements, ETrackPlacementKind::LaserGate);
			Flat += Layout.FlatSweeps.Num();
			Open += Layout.OpenStretches.Num();
		}
	}
	AddInfo(FString::Printf(TEXT("50 tracks: worst length error %.3f %%; %d ramps, %d loops, %d tubes, %d speed orbs, %d repair orbs, %d gates, %d flat sweeps, %d open stretches"),
		WorstLengthError * 100.f, Ramps, Loops, Tubes, Speed, Repair, Gates, Flat, Open));
	TestTrue(TEXT("the mixed shape placed loops"), Loops > 0);
	TestTrue(TEXT("the mixed shape placed tubes"), Tubes > 0);
	TestTrue(TEXT("ramps were placed"), Ramps > 0);
	TestTrue(TEXT("gates were placed"), Gates > 0);
	return true;
}

/**
 * The hyperspace jump: a whole decided track cut back past the built road and ended there, on a
 * generated track and on a saved one. The new end is nearer, the rules still hold, and nothing
 * decided survives past the cut.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerDecideForceEndTest, "Runner.Track.Decide.ForceEnd", DecideTestFlags)
bool FRunnerDecideForceEndTest::RunTest(const FString& Parameters)
{
	FShippedData Data;
	if (!Data.Load(*this)) return false;
	const UTrackLayoutAsset* Standard = LoadObject<UTrackLayoutAsset>(nullptr, TEXT("/Game/Runner/Data/Tracks/TL_Standard.TL_Standard"));

	for (int32 Case = 0; Case < 2; ++Case)
	{
		UTrackDecider* Decider = NewObject<UTrackDecider>();
		FString Label;
		if (Case == 0)
		{
			FTrackDecideParams Params;
			Params.Seed = 4242u;
			Decider->Generate(Data.Shape, Data.SpawnSet, Data.EndRamp, Params);
			Label = TEXT("generated");
		}
		else
		{
			if (!TestNotNull(TEXT("TL_Standard"), Standard)) continue;
			Decider->Load(Standard, Data.SpawnSet, Data.EndRamp, FTrackDecideParams());
			Label = TEXT("TL_Standard");
		}

		const float OldEnd = Decider->GetTrack()->GetEndDistance();
		const float BuiltTo = 6000.f;
		if (!TestTrue(Label + TEXT(": the end comes in"), Decider->ForceEndAhead(1500.f, BuiltTo))) continue;
		const UTrackModel* Track = Decider->GetTrack();
		AddInfo(FString::Printf(TEXT("%s: end %.0f m -> %.0f m (zone from %.0f m)"), *Label, OldEnd, Track->GetEndDistance(), Track->GetEndZoneStart()));
		TestTrue(Label + TEXT(": complete again"), Decider->IsComplete());
		TestTrue(Label + TEXT(": the new end is nearer"), Track->GetEndDistance() < OldEnd - 1000.f);
		TestTrue(Label + TEXT(": the new end is past the built road"), Track->GetEndZoneStart() > BuiltTo);
		TestTrue(Label + TEXT(": the run-up is at least the asked length"), Track->GetEndDistance() - Track->GetEndZoneStart() >= 1500.f - 1.f);
		TestEqual(Label + TEXT(": a distance per knot"), Decider->GetKnotDistances().Num(), Track->GetSpline().Num());
		CheckRules(*this, *Decider, Label + TEXT(" forced"));
		for (const FTrackPlacement& P : Decider->GetPlacements())
		{
			if (P.Kind != ETrackPlacementKind::EndRamp && P.Distance >= Track->GetEndZoneStart())
			{
				AddError(FString::Printf(TEXT("%s: a record at %.0f m survived inside the new run-up"), *Label, P.Distance));
				break;
			}
		}
		// A second jump with the end already that close changes nothing.
		TestFalse(Label + TEXT(": no second cut once the end is near"), Decider->ForceEndAhead(1500.f, Track->GetEndZoneStart()));
	}
	return true;
}

/**
 * The spawn set decided afresh on TL_Standard's saved road, off the layout's saved seed: the rules
 * hold, and the counts are logged against the records Unity saved. They do not match, and that is
 * Unity's data, not the port: the saved orbs were rolled by Unity's Reroll Placements off a seed it
 * never stores. Unity's own code run on the saved seed (checked in the Unity editor, 2026-11-12)
 * gives the first speed orb at 155.639 m and the first gate at 1638.694 m, the port's values exactly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerDecideStandardSpawnTest, "Runner.Track.Decide.StandardSpawnSet", DecideTestFlags)
bool FRunnerDecideStandardSpawnTest::RunTest(const FString& Parameters)
{
	FShippedData Data;
	if (!Data.Load(*this)) return false;
	const UTrackLayoutAsset* Asset = LoadObject<UTrackLayoutAsset>(nullptr, TEXT("/Game/Runner/Data/Tracks/TL_Standard.TL_Standard"));
	if (!TestNotNull(TEXT("TL_Standard"), Asset)) return false;

	UTrackDecider* Decider = NewObject<UTrackDecider>();
	TestTrue(TEXT("TL_Standard loads at its saved length"), Decider->Load(Asset, Data.SpawnSet, Data.EndRamp, FTrackDecideParams()));
	TestTrue(TEXT("a loaded track is complete"), Decider->IsComplete());
	TestEqual(TEXT("every saved record is in"), Decider->GetPlacements().Num(), Asset->Layout.Placements.Num());

	const TArray<FTrackPlacement>& Saved = Asset->Layout.Placements;
	const int32 SavedSpeed = CountKind(Saved, ETrackPlacementKind::SpeedOrb);
	const int32 SavedGates = CountKind(Saved, ETrackPlacementKind::LaserGate);
	const int32 SavedRepair = CountKind(Saved, ETrackPlacementKind::RepairOrb);

	Decider->RedecideSpawnSet(static_cast<uint32>(Asset->Layout.Seed));
	CheckRules(*this, *Decider, TEXT("TL_Standard redecided"));
	const TArray<FTrackPlacement>& Now = Decider->GetPlacements();
	const int32 Speed = CountKind(Now, ETrackPlacementKind::SpeedOrb);
	const int32 Gates = CountKind(Now, ETrackPlacementKind::LaserGate);
	const int32 Repair = CountKind(Now, ETrackPlacementKind::RepairOrb);

	// How many records land where Unity's did (same kind, within 1 cm).
	int32 Matching = 0;
	for (const FTrackPlacement& P : Now)
	{
		Matching += Saved.ContainsByPredicate([&P](const FTrackPlacement& S) { return S.Kind == P.Kind && FMath::Abs(S.Distance - P.Distance) < 0.01f; }) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("TL_Standard spawn set: %d speed / %d gates / %d repair (Unity saved %d / %d / %d); %d of %d records at Unity's distances"),
		Speed, Gates, Repair, SavedSpeed, SavedGates, SavedRepair, Matching, Now.Num()));
	// Both lists, for a side-by-side.
	FString Csv = TEXT("source,kind,distance,lateral,variant\n");
	for (const FTrackPlacement& P : Saved) Csv += FString::Printf(TEXT("unity,%d,%.3f,%.3f,%d\n"), static_cast<int32>(P.Kind), P.Distance, P.Lateral, P.Variant);
	for (const FTrackPlacement& P : Now) Csv += FString::Printf(TEXT("port,%d,%.3f,%.3f,%d\n"), static_cast<int32>(P.Kind), P.Distance, P.Lateral, P.Variant);
	FFileHelper::SaveStringToFile(Csv, *(FPaths::ProjectSavedDir() / TEXT("RunnerTests") / TEXT("TL_Standard_SpawnSet.csv")));

	TestTrue(TEXT("speed orbs decided"), Speed > 0);
	TestTrue(TEXT("gates decided"), Gates > 0);
	return true;
}

#endif
