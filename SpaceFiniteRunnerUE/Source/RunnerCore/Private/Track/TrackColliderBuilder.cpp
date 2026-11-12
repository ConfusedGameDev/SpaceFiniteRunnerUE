#include "Track/TrackColliderBuilder.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "Track/Features/JumpRamp.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"

const FName UTrackColliderBuilder::SurfaceTag(TEXT("TrackSurface"));
const FName UTrackColliderBuilder::GroundTag(TEXT("TrackGround"));

UTrackColliderBuilder::UTrackColliderBuilder()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTrackColliderBuilder::SetTrack(const UTrackModel* InTrack)
{
	Clear();
	Track = InTrack;
}

void UTrackColliderBuilder::SetSurfaceSink(float Metres)
{
	const float Sink = FMath::Max(0.f, Metres);
	if (FMath::IsNearlyEqual(Sink, SurfaceSink)) return;
	SurfaceSink = Sink;
	Clear();
}

void UTrackColliderBuilder::Clear()
{
	for (FChunk& Chunk : Chunks) Drop(Chunk);
	for (TPair<TWeakObjectPtr<AJumpRamp>, FChunk>& Pair : Ramps) Drop(Pair.Value);
	Chunks.Reset();
	Ramps.Reset();
	Built = 0.f;
}

void UTrackColliderBuilder::Drop(FChunk& Chunk)
{
	if (IsValid(Chunk.Component)) Chunk.Component->DestroyComponent();
	Chunk.Component = nullptr;
}

// ----------------------------------------------------------------- stream

void UTrackColliderBuilder::BuildUpTo(float Distance, TConstArrayView<TObjectPtr<AJumpRamp>> LiveRamps)
{
	if (!Track || !GetOwner()) return;
	const float Limit = FMath::Min(Distance, Track->GetLength());
	// Whole chunks only: a partial one would have to be rebuilt as the window moves on.
	while (Built + ChunkLength <= Limit)
	{
		Chunks.Add(BuildRoad(Built, Built + ChunkLength));
		Built += ChunkLength;
	}
	// The track ends where it ends, not on a chunk boundary: the remainder is one last, shorter chunk.
	const float End = Track->HasEnd() ? Track->GetEndDistance() : Track->GetLength();
	if (Limit >= End - 0.01f && Built < End - 0.01f)
	{
		Chunks.Add(BuildRoad(Built, End));
		Built = End;
	}

	for (const TObjectPtr<AJumpRamp>& Ramp : LiveRamps)
	{
		if (IsValid(Ramp) && Ramp->GetDefinition() && !Ramps.Contains(Ramp.Get()) && Ramp->GetEndDistance() <= Limit)
		{
			Ramps.Add(Ramp.Get(), BuildRamp(*Ramp));
		}
	}
}

void UTrackColliderBuilder::CullBefore(float Distance)
{
	for (int32 i = Chunks.Num() - 1; i >= 0; --i)
	{
		if (Chunks[i].End >= Distance) continue;
		Drop(Chunks[i]);
		Chunks.RemoveAt(i);
	}
	for (auto It = Ramps.CreateIterator(); It; ++It)
	{
		if (It.Key().IsValid() && It.Value().End >= Distance) continue;
		Drop(It.Value());
		It.RemoveCurrent();
	}
}

const UPrimitiveComponent* UTrackColliderBuilder::GetChunkAt(float Distance) const
{
	for (const FChunk& Chunk : Chunks)
	{
		if (Distance >= Chunk.Start && Distance < Chunk.End) return Chunk.Component;
	}
	return nullptr;
}

// ------------------------------------------------------------------- road

UTrackColliderBuilder::FChunk UTrackColliderBuilder::BuildRoad(float From, float To)
{
	Vertices.Reset();
	Triangles.Reset();

	const int32 Rows = FMath::Max(1, FMath::CeilToInt((To - From) / SampleSpacing));
	// One column count for the whole chunk, so the grid stays a grid: as many as its most curled row needs.
	int32 LaneColumns = FMath::Max(1, FlatColumns);
	bool bAnyShoulders = false;
	bool bLoop = false;
	for (int32 R = 0; R <= Rows; ++R)
	{
		const float D = FMath::Lerp(From, To, R / static_cast<float>(Rows));
		bAnyShoulders |= Track->HasShoulders(D);
		const FTrackSection* Section = Track->SectionAt(D);
		if (!Section) continue;
		bLoop |= Section->AsLoop() != nullptr;
		const FTubeSection* Tube = Section->AsTube();
		if (!Tube) continue;
		float Min, Max;
		Track->GetLateralBand(D, Min, Max);
		if (Tube->Curl(D - Tube->GetStartDistance()) > 0.001f)
		{
			LaneColumns = FMath::Max(LaneColumns, FMath::CeilToInt((Max - Min) / CurledQuadWidth));
		}
	}

	// The shoulders get columns of their own, so a crease lands exactly on a vertex ring.
	const int32 ShoulderCols = bAnyShoulders ? FMath::Max(1, ShoulderColumns) : 0;
	const int32 Columns = LaneColumns + 2 * ShoulderCols;
	const int32 Stride = Columns + 1;
	for (int32 R = 0; R <= Rows; ++R)
	{
		AddCrossSection(FMath::Lerp(From, To, R / static_cast<float>(Rows)), Columns, ShoulderCols, LaneColumns);
	}
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Columns; ++C)
		{
			const int32 L0 = R * Stride + C, R0 = L0 + 1, L1 = L0 + Stride, R1 = L1 + 1;
			// Rows run forward (+X), columns left to right (+Y): this order faces up (+Z).
			AddTri(L0, L1, R1);
			AddTri(L0, R1, R0);
		}
	}

	// A loop's two halves may cross in space: its chunks are surface-only and carry no walls (the
	// guide's lane keeps the ship on the ring).
	if (WallHeight > 0.f && !bLoop)
	{
		AddWall(From, To, Rows, -1);
		AddWall(From, To, Rows, 1);
	}
	return Emit(From, To, bLoop);
}

void UTrackColliderBuilder::AddCrossSection(float D, int32 Columns, int32 ShoulderCols, int32 LaneColumns)
{
	float Min, Max;
	Track->GetLateralBand(D, Min, Max);
	const bool bShoulders = ShoulderCols > 0 && Track->HasShoulders(D);
	const float Shoulder = bShoulders ? Track->GetShoulderWidth() : 0.f;
	const float Rise = bShoulders ? Track->GetShoulderRise() : 0.f;

	for (int32 C = 0; C <= Columns; ++C)
	{
		float Lateral, Lift;
		if (!bShoulders)
		{
			Lateral = FMath::Lerp(Min, Max, C / static_cast<float>(Columns));
			Lift = 0.f;
		}
		else if (C < ShoulderCols)
		{
			const float U = C / static_cast<float>(ShoulderCols); // 0 at the outer lip, 1 at the crease
			Lateral = Min - Shoulder * (1.f - U);
			Lift = Rise * (1.f - U);
		}
		else if (C <= ShoulderCols + LaneColumns)
		{
			Lateral = FMath::Lerp(Min, Max, (C - ShoulderCols) / static_cast<float>(LaneColumns));
			Lift = 0.f;
		}
		else
		{
			const float U = (C - ShoulderCols - LaneColumns) / static_cast<float>(ShoulderCols); // 0 at the crease, 1 at the lip
			Lateral = Max + Shoulder * U;
			Lift = Rise * U;
		}

		FVector Position;
		FQuat Rotation;
		Track->GetPoseAtDistance(D, Lateral, Position, Rotation);
		Vertices.Add(Position - Rotation.GetUpVector() * (SurfaceSink - Lift));
	}
}

void UTrackColliderBuilder::AddWall(float From, float To, int32 Rows, int32 Side)
{
	int32 Previous = INDEX_NONE;
	for (int32 R = 0; R <= Rows; ++R)
	{
		const float D = FMath::Lerp(From, To, R / static_cast<float>(Rows));
		const bool bClosed = !Track->IsEdgeOpen(D, Side) && !IsUnboundedTube(D);
		if (!bClosed)
		{
			Previous = INDEX_NONE;
			continue;
		}

		float Min, Max;
		Track->GetRoadBand(D, Min, Max);
		const float Lift = Track->HasShoulders(D) ? Track->GetShoulderRise() : 0.f;
		FVector Position;
		FQuat Rotation;
		Track->GetPoseAtDistance(D, Side < 0 ? Min : Max, Position, Rotation);
		const FVector Up = Rotation.GetUpVector();
		const FVector Foot = Position - Up * (SurfaceSink - Lift);
		const int32 Index = Vertices.Num();
		Vertices.Add(Foot);
		Vertices.Add(Foot + Up * WallHeight);

		if (Previous != INDEX_NONE)
		{
			// Each wall faces the lane.
			const int32 B0 = Previous, T0 = Previous + 1, B1 = Index, T1 = Index + 1;
			if (Side < 0)
			{
				AddTri(B0, T0, T1);
				AddTri(B0, T1, B1);
			}
			else
			{
				AddTri(B1, T1, T0);
				AddTri(B1, T0, B0);
			}
		}
		Previous = Index;
	}
}

bool UTrackColliderBuilder::IsUnboundedTube(float Distance) const
{
	const FTrackSection* Section = Track->SectionAt(Distance);
	const FTubeSection* Tube = Section ? Section->AsTube() : nullptr;
	return Tube && Tube->IsUnboundedAt(Distance - Tube->GetStartDistance());
}

// ------------------------------------------------------------------ ramps

UTrackColliderBuilder::FChunk UTrackColliderBuilder::BuildRamp(const AJumpRamp& Ramp)
{
	// As wide as the ramp's RULE, not its picture: a ship is on the ramp once its centre is
	// EntryMargin inside the edge, and is held out at that same line otherwise.
	Vertices.Reset();
	Triangles.Reset();
	const UJumpDefinition& Def = *Ramp.GetDefinition();
	const float HalfWidth = FMath::Max(1.f, Ramp.GetHalfWidth() - Def.EntryMargin);
	const int32 Rows = FMath::Max(2, FMath::CeilToInt(Ramp.GetLength() / SampleSpacing));

	auto Sample = [&](int32 R, int32 C, FVector& OutPosition, FVector& OutUp)
	{
		const float D = FMath::Lerp(Ramp.GetStartDistance(), Ramp.GetEndDistance(), R / static_cast<float>(Rows));
		const float Lateral = Ramp.GetLateral() + (C == 0 ? -HalfWidth : HalfWidth);
		FQuat Rotation;
		Track->GetPoseAtDistance(D, Lateral, OutPosition, Rotation);
		OutUp = Rotation.GetUpVector();
		return D;
	};

	// The slope, rising from the road to the lip along the track's up.
	for (int32 R = 0; R <= Rows; ++R)
	{
		for (int32 C = 0; C <= 1; ++C)
		{
			FVector Position, Up;
			const float D = Sample(R, C, Position, Up);
			const float Height = R == Rows ? Def.GetLipHeight() : Ramp.HeightAt(D);
			Vertices.Add(Position + Up * (Height - SurfaceSink));
		}
	}
	for (int32 R = 0; R < Rows; ++R)
	{
		const int32 L0 = R * 2, R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
		AddTri(L0, L1, R1);
		AddTri(L0, R1, R0);
	}

	// The flanks, facing out: the side of a ramp is a wall, never half on the slope and half on the road.
	const int32 Feet = Vertices.Num();
	for (int32 R = 0; R <= Rows; ++R)
	{
		for (int32 C = 0; C <= 1; ++C)
		{
			FVector Position, Up;
			Sample(R, C, Position, Up);
			Vertices.Add(Position - Up * SurfaceSink);
		}
	}
	for (int32 R = 0; R < Rows; ++R)
	{
		const int32 LB0 = Feet + R * 2, LB1 = LB0 + 2, LT0 = R * 2, LT1 = LT0 + 2;
		AddTri(LB1, LT1, LT0);
		AddTri(LB1, LT0, LB0);
		const int32 RB0 = LB0 + 1, RB1 = LB1 + 1, RT0 = LT0 + 1, RT1 = LT1 + 1;
		AddTri(RB0, RT0, RT1);
		AddTri(RB0, RT1, RB1);
	}
	return Emit(Ramp.GetStartDistance(), Ramp.GetEndDistance(), false);
}

// ----------------------------------------------------------------- output

UTrackColliderBuilder::FChunk UTrackColliderBuilder::Emit(float Start, float End, bool bSurfaceOnly)
{
	using namespace UE::Geometry;

	FDynamicMesh3 Mesh;
	for (const FVector& V : Vertices)
	{
		Mesh.AppendVertex(FVector3d(V * 100.0)); // track metres → the generator's local centimetres
	}
	// The triangles above are in Unity's order (front face = (B − A) × (C − A) in these axes). Chaos
	// keeps the other winding as the front, and its trimesh queries skip back faces, so each one is
	// turned round here. A degenerate one (a zero-width band) is simply refused.
	for (const FIntVector& T : Triangles)
	{
		Mesh.AppendTriangle(T.X, T.Z, T.Y);
	}

	AActor* Owner = GetOwner();
	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(Owner, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetupAttachment(Owner->GetRootComponent());
	Component->bUseAsyncCooking = false; // the ship is launched onto road that exists this frame
	Component->SetComplexAsSimpleCollisionEnabled(true, false);
	Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Component->SetCanEverAffectNavigation(false);
	Component->ComponentTags.Add(bSurfaceOnly ? SurfaceTag : GroundTag);
	Component->SetVisibility(bShowMeshes);
	Component->SetCastShadow(false);
	Component->RegisterComponent();
	Component->SetMesh(MoveTemp(Mesh));
	Component->UpdateCollision(false);

	FChunk Chunk;
	Chunk.Start = Start;
	Chunk.End = End;
	Chunk.Component = Component;
	return Chunk;
}
