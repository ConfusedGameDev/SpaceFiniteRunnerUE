#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Track/TrackPieces.h"
#include "TrackDecorator.generated.h"

class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;
class UTrackModel;
class FTubeSection;

/**
 * Stamps the road kit along the track (the Unity TrackDecorator): a road slab every RoadSpacing
 * metres, the full-width barrier piece, and where the track says an edge is open
 * (UTrackModel::IsEdgeOpen, the same flag the collision reads) the wall is left off that side:
 * a placeholder wall stands on the closed side and a low marker strip runs along the open one.
 * Tubes are cut into strips no wider than TubeStripWidth; the end ramps' gaps get end markers.
 *
 * Every piece is a USplineMeshComponent running between the track poses at its two ends, so
 * neighbours share their end poses and meet without gaps. Pieces are pooled: culled ones are
 * hidden and reused by the next stamp. The defaults are the Unity PF_Track values, measured at
 * a 120 m track.
 */
UCLASS(ClassGroup = (Runner), meta = (BlueprintSpawnableComponent))
class RUNNERCORE_API UTrackDecorator : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrackDecorator();

	// ------------------------------------------------------------- road
	/** The road slab (lane plus banked shoulders); it runs along its X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	TSoftObjectPtr<UStaticMesh> RoadMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	TSoftObjectPtr<UMaterialInterface> RoadMaterial;

	/** Metres of track between stamps. Each piece spans one spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (ClampMin = 2, Units = "m"))
	float RoadSpacing = 19.f;

	/** Where the visible road surface sits along the track's up, relative to the flight line (negative = below). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (Units = "m"))
	float RoadYOffset = -1.f;

	/** The slab's up scale and how high its origin stands over the road surface (the Unity RoadSlab prefab's child, scaled). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	float RoadUpScale = 0.543183f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (Units = "m"))
	float RoadOriginLift = 2.187077f;

	// ---------------------------------------------------------- barrier
	/** The full-width barrier tile (both walls in one piece); it runs along its Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier")
	TSoftObjectPtr<UStaticMesh> BarrierMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier")
	TSoftObjectPtr<UMaterialInterface> BarrierMaterial;

	/** Up scale of the barrier tile (Unity's 25: the 8 cm tile stands 2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier")
	float BarrierUpScale = 25.f;

	// -------------------------------------------------------- open edges
	/** The unit cube the placeholder wall and the markers are made of. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Open Edges")
	TSoftObjectPtr<UStaticMesh> BoxMesh;

	/** Placeholder wall on the closed side of a one-sided stretch: thickness and height, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Open Edges")
	FVector2D PlaceholderWallSize = FVector2D(2.f, 12.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Open Edges")
	TSoftObjectPtr<UMaterialInterface> WallMaterial;

	/** Marker strip along an open edge: width and height, metres. 0 width = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Open Edges")
	FVector2D OpenEdgeMarkerSize = FVector2D(1.5f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Open Edges")
	TSoftObjectPtr<UMaterialInterface> EdgeMaterial;

	// ------------------------------------------------------------ tubes
	/** Widest one road strip round a tube may be, metres of arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tubes", meta = (ClampMin = 2, Units = "m"))
	float TubeStripWidth = 25.f;

	/** The flat tile a tube strip is made of; it runs along its Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tubes")
	TSoftObjectPtr<UStaticMesh> TubeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tubes")
	TSoftObjectPtr<UMaterialInterface> TubeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tubes")
	float TubeUpScale = 10.f;

	// ---------------------------------------------------------- streaming
	/** The track the pieces follow. Clears everything stamped. */
	void SetTrack(const UTrackModel* InTrack);

	/** Stamps from the last stamp up to Distance. */
	void DecorateUpTo(float Distance);

	/** Hides every piece stamped before Distance (back to the pool). */
	void CullBefore(float Distance);

	/** Hides every piece and starts again from the track start. */
	void Clear();

	/** Marks a gap between two end ramps: a strip from the ramps' foot to the end of the road. */
	void StampEndMarker(float StartDistance, float LateralCentre, float Width, float Length);

	/** Distance of the next stamp. */
	float GetStampCursor() const { return StampCursor; }
	/** Pieces on show, and the stamp distance each one belongs to (for tests and the reveal). */
	int32 GetLiveCount() const { return Live.Num(); }
	void GetLiveStamps(TArray<TPair<float, const USplineMeshComponent*>>& Out) const;

private:
	struct FLivePiece
	{
		float Distance = 0.f;
		TObjectPtr<USplineMeshComponent> Component;
	};

	void Resolve();
	void StampAt(float D);
	void StampTube(float D, const FTubeSection& Tube);
	void StampPlaceholderWall(float D, int32 Side);
	void StampOpenEdgeMarker(float D, int32 Side);
	USplineMeshComponent* Stamp(float KeyDistance, float From, float To, float Lateral, UStaticMesh* Mesh, UMaterialInterface* Material, const FTrackPieceFit& Fit);
	FTrackPieceFit BoxFit(float Across, float Height, float UpCentre) const;

	UPROPERTY(Transient)
	TObjectPtr<const UTrackModel> Track;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> Pool;

	TArray<FLivePiece> Live;
	float StampCursor = 0.f;

	// Resolved once.
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Road;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Barrier;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Box;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Tube;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RoadMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BarrierMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WallMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> EdgeMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TubeMat;
	bool bResolved = false;
};
