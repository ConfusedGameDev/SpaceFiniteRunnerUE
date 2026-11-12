#include "Track/TrackDecorator.h"

#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"

UTrackDecorator::UTrackDecorator()
{
	PrimaryComponentTick.bCanEverTick = false;
	RoadMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Runner/Meshes/Track/SM_RoadStraight_v1.SM_RoadStraight_v1")));
	BarrierMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Runner/Meshes/Track/SM_RoadStraightBarrier.SM_RoadStraightBarrier")));
	TubeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Runner/Meshes/Track/SM_RoadStraight.SM_RoadStraight")));
	BoxMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	RoadMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Track/MI_NeonRoad_Mat.MI_NeonRoad_Mat")));
	BarrierMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Laser/MI_NeonBarrier_Mat.MI_NeonBarrier_Mat")));
	WallMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Track/MI_NeonWall_Mat.MI_NeonWall_Mat")));
	EdgeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Track/MI_NeonEdge_Mat.MI_NeonEdge_Mat")));
	TubeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Track/MI_NeonTube_Mat.MI_NeonTube_Mat")));
}

void UTrackDecorator::Resolve()
{
	if (bResolved) return;
	bResolved = true;
	Road = RoadMesh.LoadSynchronous();
	Barrier = BarrierMesh.LoadSynchronous();
	Box = BoxMesh.LoadSynchronous();
	Tube = TubeMesh.LoadSynchronous();
	RoadMat = RoadMaterial.LoadSynchronous();
	BarrierMat = BarrierMaterial.LoadSynchronous();
	WallMat = WallMaterial.LoadSynchronous();
	EdgeMat = EdgeMaterial.LoadSynchronous();
	TubeMat = TubeMaterial.LoadSynchronous();
	if (!BarrierMat) BarrierMat = RoadMat;
	if (!WallMat) WallMat = BarrierMat;
	if (!EdgeMat) EdgeMat = RoadMat;
	if (!TubeMat) TubeMat = RoadMat;
	if (!Tube) Tube = Box;
}

void UTrackDecorator::SetTrack(const UTrackModel* InTrack)
{
	Clear();
	Track = InTrack;
}

void UTrackDecorator::Clear()
{
	for (FLivePiece& Piece : Live)
	{
		if (Piece.Component)
		{
			Piece.Component->SetVisibility(false);
			Pool.Add(Piece.Component);
		}
	}
	Live.Reset();
	StampCursor = 0.f;
}

void UTrackDecorator::DecorateUpTo(float Distance)
{
	if (!Track || !GetOwner()) return;
	Resolve();
	if (StampCursor <= 0.f) StampCursor = RoadSpacing * 0.5f;
	const float Limit = FMath::Min(Distance, Track->GetLength());
	while (StampCursor < Limit)
	{
		StampAt(StampCursor);
		StampCursor += RoadSpacing;
	}
}

void UTrackDecorator::CullBefore(float Distance)
{
	for (int32 i = Live.Num() - 1; i >= 0; --i)
	{
		if (Live[i].Distance >= Distance) continue;
		if (USplineMeshComponent* C = Live[i].Component)
		{
			C->SetVisibility(false);
			Pool.Add(C);
		}
		Live.RemoveAtSwap(i);
	}
}

void UTrackDecorator::GetLiveStamps(TArray<TPair<float, const USplineMeshComponent*>>& Out) const
{
	Out.Reset(Live.Num());
	for (const FLivePiece& Piece : Live) Out.Emplace(Piece.Distance, Piece.Component.Get());
}

void UTrackDecorator::StampAt(float D)
{
	const FTrackSection* Section = Track->SectionAt(D);
	if (const FTubeSection* TubeSection = Section ? Section->AsTube() : nullptr)
	{
		StampTube(D, *TubeSection);
		return;
	}

	const float Half = RoadSpacing * 0.5f;
	const float From = D - Half;
	const float To = D + Half;
	const float RoadHalf = Track->GetRoadHalfWidth();

	if (Road)
	{
		FTrackPieceFit Fit;
		Fit.ForwardAxis = ESplineMeshAxis::X;
		Fit.AcrossScale = 2.f * RoadHalf * 100.f / FMath::Max(1.f, TrackPieces::AcrossExtent(*Road, Fit.ForwardAxis));
		Fit.UpScale = RoadUpScale;
		Fit.UpOffset = RoadYOffset + RoadOriginLift;
		Stamp(D, From, To, 0.f, Road, RoadMat, Fit);
	}

	const bool bOpenLeft = Track->IsEdgeOpen(D, -1);
	const bool bOpenRight = Track->IsEdgeOpen(D, 1);
	if (bOpenLeft) StampOpenEdgeMarker(D, -1);
	if (bOpenRight) StampOpenEdgeMarker(D, 1);

	if (!Barrier || (bOpenLeft && bOpenRight)) return; // both walls gone: nothing to show
	if (bOpenLeft != bOpenRight)
	{
		// The full-width piece carries both walls: a placeholder wall stands on the closed side.
		StampPlaceholderWall(D, bOpenLeft ? 1 : -1);
		return;
	}

	FTrackPieceFit Fit;
	Fit.ForwardAxis = ESplineMeshAxis::Y;
	Fit.AcrossScale = 2.f * RoadHalf * 100.f / FMath::Max(1.f, TrackPieces::AcrossExtent(*Barrier, Fit.ForwardAxis));
	Fit.UpScale = BarrierUpScale;
	Fit.UpOffset = RoadYOffset;
	Stamp(D, From, To, 0.f, Barrier, BarrierMat, Fit);
}

FTrackPieceFit UTrackDecorator::BoxFit(float Across, float Height, float UpCentre) const
{
	// The engine cube is 1 m, centred: its origin is its middle.
	FTrackPieceFit Fit;
	Fit.ForwardAxis = ESplineMeshAxis::X;
	Fit.AcrossScale = Across;
	Fit.UpScale = Height;
	Fit.UpOffset = UpCentre;
	return Fit;
}

void UTrackDecorator::StampPlaceholderWall(float D, int32 Side)
{
	if (!Box) return;
	const float Thickness = FMath::Max(0.1f, PlaceholderWallSize.X);
	const float Height = FMath::Max(0.1f, PlaceholderWallSize.Y);
	const float Lateral = Side * (Track->GetRoadHalfWidth() + Thickness * 0.5f);
	const float Lift = Track->HasShoulders(D) ? Track->GetShoulderRise() : 0.f;
	const float Half = RoadSpacing * 0.5f;
	Stamp(D, D - Half, D + Half, Lateral, Box, WallMat, BoxFit(Thickness, Height, RoadYOffset + Lift + Height * 0.5f));
}

void UTrackDecorator::StampOpenEdgeMarker(float D, int32 Side)
{
	if (!Box || OpenEdgeMarkerSize.X <= 0.f) return;
	const float Height = FMath::Max(0.05f, OpenEdgeMarkerSize.Y);
	const float Lateral = Side * (Track->GetRoadHalfWidth() - OpenEdgeMarkerSize.X * 0.5f);
	const float Lift = Track->HasShoulders(D) ? Track->GetShoulderRise() : 0.f;
	const float Half = RoadSpacing * 0.5f;
	Stamp(D, D - Half, D + Half, Lateral, Box, EdgeMat, BoxFit(OpenEdgeMarkerSize.X, Height, Lift + Height * 0.5f));
}

void UTrackDecorator::StampEndMarker(float StartDistance, float LateralCentre, float Width, float Length)
{
	if (!Track || !GetOwner() || Width <= 0.f || Length <= 0.f) return;
	Resolve();
	if (!Box) return;
	const float Height = FMath::Max(0.05f, OpenEdgeMarkerSize.Y);
	// Keyed on its far end for the cull.
	Stamp(StartDistance + Length, StartDistance, StartDistance + Length, LateralCentre, Box, EdgeMat, BoxFit(Width, Height, Height * 0.5f));
}

void UTrackDecorator::StampTube(float D, const FTubeSection& TubeSection)
{
	// The lane at this distance (an arc, easing in over the curl) cut into equal strips, each a
	// flat tile at its own lateral: the section's pose bends the road. No barriers on a tube.
	if (!Tube) return;
	float Min, Max;
	Track->GetLateralBand(D, Min, Max);
	const float BandWidth = FMath::Max(1.f, Max - Min);
	const int32 Strips = FMath::Max(1, FMath::CeilToInt(BandWidth / FMath::Max(2.f, TubeStripWidth)));
	const float StripWidth = BandWidth / Strips;

	FTrackPieceFit Fit;
	Fit.ForwardAxis = ESplineMeshAxis::Y;
	Fit.AcrossScale = StripWidth * 100.f / FMath::Max(1.f, TrackPieces::AcrossExtent(*Tube, Fit.ForwardAxis));
	Fit.UpScale = TubeUpScale;
	Fit.UpOffset = RoadYOffset;
	const float Half = RoadSpacing * 0.5f;
	for (int32 i = 0; i < Strips; ++i)
	{
		const float Lateral = Min + (i + 0.5f) * StripWidth;
		Stamp(D, D - Half, D + Half, Lateral, Tube, TubeMat, Fit);
	}
}

USplineMeshComponent* UTrackDecorator::Stamp(float KeyDistance, float From, float To, float Lateral, UStaticMesh* Mesh, UMaterialInterface* Material, const FTrackPieceFit& Fit)
{
	AActor* Owner = GetOwner();
	USplineMeshComponent* Piece = nullptr;
	while (!Piece && Pool.Num() > 0)
	{
		Piece = Pool.Pop(EAllowShrinking::No);
		if (!IsValid(Piece)) Piece = nullptr;
	}
	if (!Piece)
	{
		Piece = NewObject<USplineMeshComponent>(Owner, NAME_None, RF_Transient);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision); // a picture only: the collider builder is the floor
		Piece->SetCanEverAffectNavigation(false);
		Piece->SetupAttachment(Owner->GetRootComponent());
		Piece->RegisterComponent();
	}

	Piece->SetStaticMesh(Mesh);
	Piece->SetMaterial(0, Material);
	for (int32 Slot = 1; Slot < Piece->GetNumMaterials(); ++Slot) Piece->SetMaterial(Slot, Material);
	TrackPieces::Pose(*Piece, *Track, From, To, Lateral, Fit);
	Piece->SetVisibility(true);
	Live.Add({ KeyDistance, Piece });
	return Piece;
}
