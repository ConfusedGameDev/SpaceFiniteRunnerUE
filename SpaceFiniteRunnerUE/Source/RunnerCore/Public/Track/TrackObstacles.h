#pragma once

#include "CoreMinimal.h"

class AJumpRamp;
class UPickupRegistry;

/** One blocked span of road: where along the track it starts and ends, and the lateral band it closes (metres). */
struct RUNNERCORE_API FTrackObstacle
{
	float Start = 0.f;
	float End = 0.f;
	float Min = 0.f;
	float Max = 0.f;
};

/**
 * The lateral spans of road something has to steer ROUND on a stretch of track (the Unity
 * TrackObstacles): every built jump ramp (end ramps aside: nothing drives the run-up but the ship) and
 * every laser gate, beams plus emitters. It reads the BUILT objects (the generator's live ramps, the
 * gates in the UPickupRegistry), so it answers for generated and saved tracks alike, but only inside the
 * stream window. Plain queries, no state: the oncoming traffic steers by it (M7).
 */
struct RUNNERCORE_API FTrackObstacles
{
	/** Adds every ramp and gate touching From..To, its lateral band widened by Pad each side. */
	static void Collect(TConstArrayView<TObjectPtr<AJumpRamp>> Ramps, const UPickupRegistry* Registry, float From, float To, float Pad,
		TArray<FTrackObstacle>& Out);

	/** The parts of Min..Max no obstacle closes, as (from, to) intervals in increasing order (cleared first). */
	static void FreeIntervals(float Min, float Max, TConstArrayView<FTrackObstacle> Blocked, TArray<FVector2f>& Free);

	/** The point of Free closest to Lateral; false when there is none. */
	static bool Nearest(TConstArrayView<FVector2f> Free, float Lateral, float& OutNearest);
};
