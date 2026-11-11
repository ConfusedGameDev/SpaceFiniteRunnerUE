#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Track/RunnerSpline.h"
#include "Track/TrackLayout.h"
#include "TrackModel.generated.h"

class FTrackSection;
class UTrackShape;

/**
 * The track as a function of distance (the Unity TrackManager). Owns the spline and turns
 * (track distance, lateral) into poses for everything on the track. Distance from the start is
 * the authoritative coordinate: the spline grows while a track is decided, so nothing ever keeps
 * a spline parameter.
 *
 * Sections are stretches with their own pose: a loop inserts track distance, a tube overlays the
 * spline. Track distance = spline distance + every insert before it, so Length is the whole track
 * and GetPoseAtDistance routes a distance inside a section to it.
 *
 * Everything here is in TRACK SPACE: metres, Unreal axes, relative to the generator actor.
 * ToWorld converts to centimetres in the world, at the engine boundary.
 */
UCLASS(BlueprintType)
class RUNNERCORE_API UTrackModel : public UObject
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------- width
	/** Sets the playable lane from a full track width. */
	void SetWidth(float FullWidth) { HalfWidth = FMath::Max(0.5f, FullWidth * 0.5f); }

	UFUNCTION(BlueprintPure, Category = "Track")
	float GetHalfWidth() const { return HalfWidth; }

	/** Width of one banked shoulder: the run-off outside the lane, on both sides of the plain road. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetShoulderWidth() const { return HalfWidth * FMath::Max(0.f, ShoulderFraction); }

	/** How far the top of a shoulder stands above the road surface. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetShoulderRise() const { return FMath::Max(0.f, ShoulderRise); }

	/** Half width of the whole road slab: the lane plus its shoulder. Where the walls stand. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetRoadHalfWidth() const { return HalfWidth + GetShoulderWidth(); }

	// ------------------------------------------------------------ length
	/** Whole track length in track distance: the spline plus every inserted section. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetLength() const { return Length; }

	UFUNCTION(BlueprintPure, Category = "Track")
	float GetSplineLength() const { return SplineLength; }

	void Recalculate();

	/** Bumped by every ClearKnots / TruncateKnots: anything cached along the track is stale. */
	int32 GetRevision() const { return Revision; }

	// -------------------------------------------------------------- end
	UFUNCTION(BlueprintPure, Category = "Track")
	bool HasEnd() const { return EndDistance >= 0.f; }

	/** Track distance where the road stops, or -1 while the track is still growing. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetEndDistance() const { return EndDistance; }

	/** Track distance where the final run-up begins, or -1. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetEndZoneStart() const { return EndZoneStart; }

	void SetEndZone(float StartDistance) { EndZoneStart = StartDistance; }
	void SetEnd(float Distance) { EndDistance = Distance; }

	// ------------------------------------------------------------ knots
	const FRunnerSpline& GetSpline() const { return Spline; }

	/** The knots as handed in (before AutoSmooth re-derived them): what a saved track stores. */
	const TArray<FTrackKnot>& GetAppendedKnots() const { return Appended; }

	void ClearKnots();
	void TruncateKnots(int32 KeepKnots, float CutDistance);

	/** Appends a saved knot exactly as it was first handed in. */
	void AppendKnot(const FTrackKnot& Knot);
	/** Appends an auto-smoothed knot at a position. */
	void AppendKnot(const FVector& Position);
	/** Appends an auto-smoothed knot carrying an authored rotation (its up survives, leaned onto the tangent plane). */
	void AppendKnot(const FVector& Position, const FQuat& Rotation);
	/** Appends a Continuous knot with an explicit WORLD-space (track-space) tangent: fixed the moment it is laid. */
	void AppendKnot(const FVector& Position, const FQuat& Rotation, const FVector& Tangent);

	/**
	 * Puts a saved track's road in place: the knots re-added in order (AutoSmooth knots re-derive
	 * their tangents off the same neighbours), each section replayed from its saved random state
	 * through the shape's feature table, the spans and the end. Returns false when the rebuilt
	 * length is more than a metre off the saved one (logged).
	 */
	bool LoadRoad(const FTrackLayout& Layout, const UTrackShape* Shape);

	/** Track distance of knot Index: its spline distance plus every insert before it. */
	float KnotTrackDistance(int32 Index) const;

	// --------------------------------------------------------- sections
	void AddSection(TSharedPtr<FTrackSection> Section);
	const TArray<TSharedPtr<FTrackSection>>& GetSections() const { return Sections; }
	FTrackSection* SectionAt(float Distance) const;
	/** Spline distance for a track distance (inside an insert: the insert's start). */
	float SplineDistanceOf(float Distance) const;

	// ------------------------------------------------------------ bands
	/** The steering lane at a distance: ±half width on the road, a section's own band inside one. */
	UFUNCTION(BlueprintPure, Category = "Track")
	void GetLateralBand(float Distance, float& OutMin, float& OutMax) const;

	/** True where the plain road's banked shoulders run: everywhere but inside a loop or a tube. */
	UFUNCTION(BlueprintPure, Category = "Track")
	bool HasShoulders(float Distance) const;

	/** All the road at a distance: the lane widened by a shoulder each side. Walls and collision end here. */
	UFUNCTION(BlueprintPure, Category = "Track")
	void GetRoadBand(float Distance, float& OutMin, float& OutMax) const;

	// ------------------------------------------------------------ poses
	/** The spline's own pose at a SPLINE distance, shifted across. What an overlay section shapes. */
	void GetSplinePoseAtDistance(float SplineDistance, float Lateral, FVector& OutPosition, FQuat& OutRotation) const;

	/** Pose at a TRACK distance: a section's own inside one, the spline's elsewhere, run on straight past the end. */
	void GetPoseAtDistance(float Distance, float Lateral, FVector& OutPosition, FQuat& OutRotation) const;

	/** Blueprint form of GetPoseAtDistance, in track space (metres). */
	UFUNCTION(BlueprintPure, Category = "Track", meta = (DisplayName = "Get Pose At Distance"))
	FTransform GetPoseTransform(float Distance, float Lateral) const;

	void GetFrameAtDistance(float Distance, FVector& OutPosition, FVector& OutForward, FVector& OutUp, FVector& OutRight) const;

	/** Signed turn rate of the centre line in 1/m, positive to the right. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetCurvatureAtDistance(float Distance) const;

	/** Roll of the road against the level, degrees, right edge up positive. 0 where forward is vertical. */
	UFUNCTION(BlueprintPure, Category = "Track")
	float GetBankAtDistance(float Distance) const;

	// ------------------------------------------------------------ spans
	/** Registers a flat sweep; returns its index so the generator can push its end out. */
	int32 AddFlatSweep(float StartDistance, float EndDistance, int32 OuterSide);
	int32 AddOpenStretch(float StartDistance, float EndDistance);
	void SetFlatSweepEnd(int32 Index, float End) { FlatSweeps[Index].End = End; }
	void SetOpenStretchEnd(int32 Index, float End) { OpenStretches[Index].End = End; }
	const TArray<FTrackSpan>& GetFlatSweeps() const { return FlatSweeps; }
	const TArray<FTrackSpan>& GetOpenStretches() const { return OpenStretches; }

	/** The flat sweep covering a distance, or null. Never inside a section. */
	const FTrackSpan* FlatSweepAt(float Distance) const;
	/** The first flat sweep that covers Distance or starts within Ahead metres of it. */
	const FTrackSpan* FlatSweepWithin(float Distance, float Ahead) const;

	/** True where the road has no wall on that side (-1 left, +1 right). */
	UFUNCTION(BlueprintPure, Category = "Track")
	bool IsEdgeOpen(float Distance, int32 Side) const;

	// ------------------------------------------------------------ world
	void SetTrackToWorld(const FTransform& InTransform) { TrackToWorld = InTransform; }
	const FTransform& GetTrackToWorld() const { return TrackToWorld; }

	/** Track-space metres to world centimetres. */
	FVector ToWorldPosition(const FVector& TrackPosition) const { return TrackToWorld.TransformPosition(TrackPosition * 100.0); }
	FQuat ToWorldRotation(const FQuat& TrackRotation) const { return TrackToWorld.GetRotation() * TrackRotation; }
	/** The pose at a distance, in the world (cm). */
	UFUNCTION(BlueprintPure, Category = "Track")
	FTransform GetWorldPose(float Distance, float Lateral) const;

	/** Banked shoulder outside the lane on each side, as a share of the lane's half width (the road slab's bank). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track", meta = (ClampMin = 0))
	float ShoulderFraction = 0.4844f;

	/** How far the top of the shoulder rises above the road surface, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track", meta = (ClampMin = 0, Units = "m"))
	float ShoulderRise = 5.1f;

private:
	void AddKnot(const FTrackKnot& Knot);

	static constexpr float CurvatureHalfStep = 5.f;

	FRunnerSpline Spline;
	TArray<FTrackKnot> Appended;
	TArray<TSharedPtr<FTrackSection>> Sections; // sorted by start
	TArray<FTrackSpan> FlatSweeps;              // in laying order, so sorted by start
	TArray<FTrackSpan> OpenStretches;           // likewise
	FTransform TrackToWorld = FTransform::Identity;

	float HalfWidth = 30.f;
	float Length = 0.f;
	float SplineLength = 0.f;
	float EndDistance = -1.f;
	float EndZoneStart = -1.f;
	int32 Revision = 0;
};
