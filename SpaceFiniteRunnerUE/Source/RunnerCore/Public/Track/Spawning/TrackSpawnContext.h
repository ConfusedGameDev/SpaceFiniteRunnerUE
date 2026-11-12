#pragma once

#include "CoreMinimal.h"
#include "Math/Interval.h"
#include "Track/RunnerRandom.h"
#include "Track/TrackLayout.h"

class AActor;
class IStreamFocus;
class ITrackRunRules;
class UPadDefinition;
class UTrackModel;

/**
 * What every spawner sees during a generator pass: the track, the run's rules (null in a sandbox),
 * the pad size, the layout seed, and the bookkeeping the generator and the spawners share (ground
 * claims, pickup distances, feature keep-outs, placement records, built actors). The port of Unity's
 * TrackSpawnContext; the context owns the lists, the generator owns the context.
 */
class RUNNERCORE_API FTrackSpawnContext
{
public:
	/** Defaults when no run rules are bound (GameSettings.Default in Unity). */
	static constexpr float DefaultAirLaneHeight = 30.f;
	static constexpr float DefaultPowerUpSpeedBoost = 52.5f;

	UTrackModel* Track = nullptr;
	const ITrackRunRules* Rules = nullptr;
	/** Base pad size (width, height, length), metres. */
	FVector3f PadSize = FVector3f(10.f, 0.5f, 20.f);
	uint32 LayoutSeed = 0;
	/** Visible road surface relative to the flight line, metres (negative = below). */
	float RoadSurfaceOffset = -1.2f;
	/** The stream focus, when one is bound. */
	TFunction<IStreamFocus*()> FocusSource;
	/** Where built actors spawn. */
	TWeakObjectPtr<UWorld> World;
	/** Built actors attach to this one (the generator). */
	TWeakObjectPtr<AActor> Parent;

	/** Ground held by something (a gate, a feature): (start, end) in track metres. */
	TArray<FFloatInterval> Claims;
	/** Every pickup's distance, so others keep a pad length off it. */
	TArray<float> PickupDistances;
	/** Stretches no gate may stand in (features with their exclusion, the end zone). End may be +inf. */
	TArray<FFloatInterval> FeatureKeepOuts;
	/** The decided records, in decide order. */
	TArray<FTrackPlacement> Placements;
	/** Built actors keyed on the distance they end at, for culling behind the focus. */
	TArray<TPair<float, TWeakObjectPtr<AActor>>> Spawned;

	void Reset();

	IStreamFocus* GetFocus() const { return FocusSource ? FocusSource() : nullptr; }

	void Emit(const FTrackPlacement& Placement) { Placements.Add(Placement); }

	float GetAirLaneHeight() const;
	float GetBaseBoost() const;

	/** Where the claim covering Distance (widened by a pad length) ends, or -1 when none does. */
	float ClaimEnd(float Distance) const;
	void Claim(float Start, float End) { Claims.Add(FFloatInterval(Start, End)); }

	/** A pickup stands within a pad length of Distance. */
	bool NearPickup(float Distance) const;
	void RecordPickup(float Distance) { PickupDistances.Add(Distance); }

	void Register(float EndDistance, AActor* Actor) { Spawned.Emplace(EndDistance, Actor); }

	/**
	 * Something a span of +-Reach round Distance may not overlap: a feature keep-out, a flat sweep (when
	 * not allowed), a section, an open edge (when not allowed). Returns the distance to retry from
	 * (+inf = never again), or -1 when the span is clear.
	 */
	float KeepOutUntil(float Distance, float Reach, bool bAllowFlatSweeps, bool bAllowOpenEdges) const;

	/**
	 * Spawns an actor of Class on the track pose at Distance and Lateral, lifted Up metres along the
	 * track's up, attached to Parent (transient, never saved). Null when there is no world or track.
	 */
	AActor* SpawnOnTrack(UClass* Class, float Distance, float Lateral, float Up = 0.f) const;
	template <class T> T* SpawnOnTrack(float Distance, float Lateral, float Up = 0.f) const
	{
		return Cast<T>(SpawnOnTrack(T::StaticClass(), Distance, Lateral, Up));
	}

	/** A lateral inside the band at Distance, Margin off both edges (the band's middle when it does not fit). */
	float RandomLateral(FRunnerRandom& Rng, float Distance, float Margin) const;
	/** Lateral pulled inside the band at Distance, Margin off both edges. */
	float ClampLateral(float Distance, float Lateral, float Margin) const;
	/** Half a pad's width plus 2 m plus its sway: how far a pad's centre keeps off the edge. */
	float PadMargin(const UPadDefinition* Definition, float Sway = 0.f) const;
};
