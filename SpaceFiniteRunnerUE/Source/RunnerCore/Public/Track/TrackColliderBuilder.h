#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrackColliderBuilder.generated.h"

class AJumpRamp;
class UDynamicMeshComponent;
class UPrimitiveComponent;
class UTrackModel;

/**
 * Gives the track a body (the Unity TrackColliderBuilder): chunk by chunk it samples the track's
 * pose across the whole road (UTrackModel::GetRoadBand) into a collision mesh. It never
 * special-cases a feature: a loop, a curled tube and a banked sweep all come out of the one pose
 * function, so physics and track space can never disagree about where the road is.
 *
 * The road is wider than the lane: a banked shoulder climbs to the slab's outer lip each side, and
 * the walls stand on that lip wherever UTrackModel::IsEdgeOpen says the edge is closed (and not in
 * an unbounded tube). Loop chunks carry no walls and are tagged TrackSurface (ridden by the hover
 * probes, not by the hull sweep, M3). A jump ramp gets a wedge with flanks as wide as its rule.
 *
 * The surface is sunk by the ship's hover height, so a ship hovering over it rides the flight line.
 * Chunks are Dynamic Mesh components, complex-as-simple, cooked synchronously, in the generator's
 * local space (cm). A chunk is culled by its END: a loop is hundreds of metres of track.
 */
UCLASS(ClassGroup = (Runner), meta = (BlueprintSpawnableComponent))
class RUNNERCORE_API UTrackColliderBuilder : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrackColliderBuilder();

	/** Tag on loop chunks: ride surface only. */
	static const FName SurfaceTag;
	/** Tag on every other chunk and the ramp wedges. */
	static const FName GroundTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ClampMin = 0, ClampMax = 60, Units = "m"))
	float WallHeight = 14.f;

	/** Track distance per collision chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meshing", meta = (ClampMin = 40, ClampMax = 400, Units = "m"))
	float ChunkLength = 120.f;

	/** Distance between cross-sections. A loop or a tube needs it small; a straight does not care. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meshing", meta = (ClampMin = 1, ClampMax = 20, Units = "m"))
	float SampleSpacing = 4.f;

	/** Quads across flat road: a road whose bank is changing is a twisted strip, and one quad folds it on a diagonal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meshing", meta = (ClampMin = 1, ClampMax = 16))
	int32 FlatColumns = 4;

	/** Widest a quad may be across the lane where the road is curled (a tube). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meshing", meta = (ClampMin = 2, ClampMax = 40, Units = "m"))
	float CurledQuadWidth = 10.f;

	/** Quads up each banked shoulder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meshing", meta = (ClampMin = 1, ClampMax = 8))
	int32 ShoulderColumns = 2;

	/** Draw the collision meshes (they are invisible otherwise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bShowMeshes = false;

	void SetTrack(const UTrackModel* InTrack);

	/** The ship's hover height, set at run start (0 until then). Changing it clears the built colliders. */
	void SetSurfaceSink(float Metres);
	float GetSurfaceSink() const { return SurfaceSink; }

	/** Builds whole chunks up to Distance (and the last, shorter one once the end is reached) and the wedges of ramps that end before it. */
	void BuildUpTo(float Distance, TConstArrayView<TObjectPtr<AJumpRamp>> Ramps);

	/** Drops every chunk and wedge that ends before Distance. */
	void CullBefore(float Distance);

	void Clear();

	/** Track distance the road chunks reach. */
	float GetBuiltDistance() const { return Built; }
	int32 GetChunkCount() const { return Chunks.Num(); }
	/** The live road chunk covering track distance Distance, or null. */
	const UPrimitiveComponent* GetChunkAt(float Distance) const;
	int32 GetRampCount() const { return Ramps.Num(); }

private:
	struct FChunk
	{
		float Start = 0.f;
		float End = 0.f;
		TObjectPtr<UDynamicMeshComponent> Component;
	};

	FChunk BuildRoad(float From, float To);
	FChunk BuildRamp(const AJumpRamp& Ramp);
	void AddCrossSection(float D, int32 Columns, int32 ShoulderCols, int32 LaneColumns);
	void AddWall(float From, float To, int32 Rows, int32 Side);
	bool IsUnboundedTube(float Distance) const;
	FChunk Emit(float Start, float End, bool bSurfaceOnly);
	void Drop(FChunk& Chunk);
	void AddTri(int32 A, int32 B, int32 C) { Triangles.Add(FIntVector(A, B, C)); }

	UPROPERTY(Transient)
	TObjectPtr<const UTrackModel> Track;

	TArray<FChunk> Chunks;
	TMap<TWeakObjectPtr<AJumpRamp>, FChunk> Ramps;

	// Scratch for the chunk being built, track metres.
	TArray<FVector> Vertices;
	TArray<FIntVector> Triangles;

	float Built = 0.f;
	float SurfaceSink = 0.f;
};
