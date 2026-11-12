#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Track/RunnerRandom.h"
#include "Track/Spawning/TrackSpawnContext.h"
#include "Track/TrackLayout.h"
#include "TrackDecider.generated.h"

class AActor;
class FLoopSection;
class IShipPerformance;
class IStreamFocus;
class ITrackRunRules;
class UJumpDefinition;
class UTrackFeatureDefinition;
class UTrackLayoutAsset;
class UTrackModel;
class UTrackShape;
class UTrackSpawner;
class UTrackSpawnSet;
struct FTrackFeatureEntry;

/** How a run's track is decided: the seed and the overrides of what the run's rules give. */
struct FTrackDecideParams
{
	/** 0 = a different layout every time (the rolled seed is GetLastSeed). */
	uint32 Seed = 0;
	/** Target track length, metres. 0 = the run's rules, else the GameSettings default. */
	float TargetLength = 0.f;
	/** Pad footprint (width, thickness, length): what the spawners scale from and keep apart by. */
	FVector3f PadSize = FVector3f(10.f, 0.5f, 20.f);
	/** The visible road surface relative to the flight line (the decorator's), metres. */
	float RoadSurfaceOffset = -1.2f;
	/** Where built actors spawn and what they attach to (the generator); null while only deciding. */
	UWorld* World = nullptr;
	AActor* Parent = nullptr;
};

/**
 * The DECIDE half of the Unity TrackGenerator: lays the whole road of a finite track at run start
 * (knots, sweeps, banks, grade, flat sweeps, open straights, features, the final run-up, the end)
 * and decides everything that goes on it as FTrackPlacement records. Every random draw of a track
 * happens here; the build half (ATrackGenerator) turns records into actors inside the stream
 * window and draws nothing, so the same records always build the same track.
 *
 * The decider plays copies of the shape, the feature definitions, the end ramp and the spawners
 * (gameplay never writes a DataAsset). It needs no world, so a test decides tracks on its own.
 * Ported rule for rule, not seed for seed (OQ3): a saved track is the exact-parity path.
 *
 * Unity's endless mode (a preview that decides as it streams) is not ported: a track is always
 * finite and always decided whole (R1.1). Coins (Collectibles) are not ported (PRD §3).
 */
UCLASS()
class RUNNERCORE_API UTrackDecider : public UObject
{
	GENERATED_BODY()

public:
	/** GameSettings.Default, used while no run rules are bound. */
	static constexpr float DefaultTrackLength = 33000.f;
	static constexpr float DefaultEndRunUp = 1200.f;
	static constexpr float DefaultEndRampGap = 10.f;
	/** Chord of one segment between knots, metres (Unity's segmentLength). */
	static constexpr float SegmentMin = 300.f;
	static constexpr float SegmentMax = 420.f;
	/** Room the hyperspace cut-back keeps between the built road and the cut. */
	static constexpr float CutClearance = 200.f;

	/** The collaborators, by contract only. Any may be null (that part falls back to the defaults). Takes effect on the next Generate. */
	void Bind(IStreamFocus* InFocus, IShipPerformance* InPerformance, const ITrackRunRules* InRules);

	/**
	 * Decides a whole track from scratch with Shape's road, its feature table and SpawnSet's spawners.
	 * EndRamp is the three end ramps' definition (null: built-in numbers).
	 */
	void Generate(const UTrackShape* Shape, const UTrackSpawnSet* SpawnSet, const UJumpDefinition* EndRamp, const FTrackDecideParams& Params);

	/**
	 * Puts a saved track in place of deciding one: its road (UTrackModel::LoadRoad), keep-outs and a
	 * copy of every record. Nothing more is decided. FallbackSpawnSet plays when the asset names none.
	 * Returns false when the asset is empty (nothing loaded) or rebuilt to another length (loaded, logged).
	 */
	bool Load(const UTrackLayoutAsset* Asset, const UTrackSpawnSet* FallbackSpawnSet, const UJumpDefinition* EndRamp, const FTrackDecideParams& Params);

	/**
	 * On a loaded track: drops the spawn set's records (orbs, repair orbs, gates) and decides them
	 * again off a new seed, on the road as it stands. Ramps, loops and the run-up are claimed first,
	 * so everything decided keeps off them by the usual rules. The editor's Reroll Placements.
	 */
	void RedecideSpawnSet(uint32 Seed);

	/**
	 * Brings the end in now (the hyperspace jump): cuts the road back past BuiltTo and lays a run-up
	 * of RunUpMeters and the end ramps from there. False when the real end is already that close.
	 */
	bool ForceEndAhead(float RunUpMeters, float BuiltTo);

	/** The decided track as data: what the editor saves. A copy, safe to keep. */
	FTrackLayout CaptureLayout() const;

	/** Drops the bookkeeping (claims, pickups, keep-outs) that ends before MinDistance. */
	void CullBehind(float MinDistance);

	// ---------------------------------------------------------------- queries
	UTrackModel* GetTrack() const { return Track; }
	const UTrackShape* GetShape() const { return Shape; }
	const TArray<TObjectPtr<UTrackSpawner>>& GetSpawners() const { return Spawners; }
	const UJumpDefinition* GetEndRamp() const { return EndRamp; }
	FTrackSpawnContext& GetContext() { return Ctx; }
	const FTrackSpawnContext& GetContext() const { return Ctx; }
	const TArray<FTrackPlacement>& GetPlacements() const { return Ctx.Placements; }
	const TArray<float>& GetKnotDistances() const { return KnotDistances; }
	const TArray<FTrackSectionRecord>& GetSectionRecords() const { return SectionRecords; }
	const TArray<FFloatInterval>& GetKeepOuts() const { return Ctx.FeatureKeepOuts; }

	/** The seed the last Generate ran on (the rolled one when the params' seed was 0). */
	uint32 GetLastSeed() const { return LastSeed; }
	float GetTargetLength() const { return TargetLength; }
	float GetEndRunUp() const { return EndRunUp; }
	/** The last knot is down: nothing more is ever appended. */
	bool IsComplete() const { return bComplete; }
	/** The track this decider plays was loaded from a saved one. */
	bool IsLoaded() const { return bLoaded; }

	/** Where the road stops: the built end once the last knot is down, the target until then. */
	float GetEndDistance() const;

	/** The feature table entry at Index of the shape in force, or null. */
	const FTrackFeatureEntry* FeatureEntry(int32 Index) const;

	/** True when no feature footprint, ramp landing or the final run-up lies anywhere in [From, To] (the patrol's attack run). */
	bool IsGroundClear(float From, float To) const;

	/** The ground a ramp at Distance keeps everything off: its footprint plus its longest landing. */
	FFloatInterval RampKeepOut(int32 FeatureIndex, float Distance) const;

	/** The jump strength landing zones are sized for: the strongest any ship can make (R1.10). */
	static float DesignJumpStrength(const UJumpDefinition& Jump);

	/** Length of one end ramp, metres. */
	float GetEndRampLength() const;
	/** Half the width of one end ramp, metres. */
	float GetEndRampHalfWidth() const;
	/** The three end ramps' centre laterals (left, middle, right), valid before they are built. */
	TArray<float> EndRampLaterals() const;
	/** The end ramps' width and gaps: one rule shared by the records, the build and the autopilot. Returns the gap between ramps. */
	float EndRampLayout(float& OutWidth, float& OutSideGap) const;
	/** Ramp I (-1 left, 0 middle, 1 right): its centre lateral and half width. */
	float EndRampLateral(int32 I, float Width, float Gap, float& OutHalf) const;

	/** Records decided since the last call (the build queue's intake), in decide order. */
	int32 TakeNewPlacements(TArray<FTrackPlacement>& Out);

private:
	/** What the knot AddSegment just laid landed on. */
	enum class ESpot : uint8 { None, Feature, EndZoneStart, End };

	void PrepareCopies(const UTrackShape* InShape, const UTrackSpawnSet* InSpawnSet, const UJumpDefinition* InEndRamp);
	void ResetState(const FTrackDecideParams& Params);
	void BeginContext(const FTrackDecideParams& Params);

	void Decide(float Target);
	void DecidePlacementsUpTo(float Limit);
	ESpot AddSegment();
	void GrowFlatSweep();
	bool LevelRequired() const;
	float NextLevelSpot() const;
	bool TurnFits(bool bFlat) const;
	void StartTurn();
	void DecideFeature();
	bool LoopReachable(const FTrackFeatureEntry& Entry, float Distance) const;
	void ContinueFromLoopExit(FLoopSection& Loop);
	void BeginEndZone();
	void FinishTrack();
	void CreateEndRamps();
	void SpawnPendingRamps(float Limit);
	void DecideJump(float Distance, int32 FeatureIndex, const UJumpDefinition& Jump);
	float PlaceSpawnersUpTo(float Limit);
	float StageGuard() const { return bComplete ? 0.f : Ctx.PadSize.Z + 1.f; }
	float DecidedDistance() const;
	float EndZoneStartFromTip() const;
	bool CutBackForEnd(float BuiltTo);
	void ResumeBuilderAt(const FVector& Position, const FQuat& Rotation, float KnotBank, float PreviousKnotDistance);
	bool IsFinite() const { return TargetLength > 0.f; }
	bool HasFeatures() const;
	float TrackLength() const;

	UPROPERTY(Transient)
	TObjectPtr<UTrackModel> Track;

	/** Runtime copies: the shape (its feature table points at copies of the definitions), the spawners, the end ramp. */
	UPROPERTY(Transient)
	TObjectPtr<UTrackShape> Shape;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTrackSpawner>> Spawners;

	UPROPERTY(Transient)
	TObjectPtr<UJumpDefinition> EndRamp;

	IStreamFocus* Focus = nullptr;
	IShipPerformance* Performance = nullptr;
	const ITrackRunRules* Rules = nullptr;

	FTrackSpawnContext Ctx;
	FRunnerRandom Rng;
	/** Ramp laterals draw from a stream of their own, so the road never depends on when they are decided. */
	FRunnerRandom RampRng;
	uint32 LastSeed = 0;

	// The builder walk, as Unity names it.
	float Heading = 0.f;           // degrees, positive to the right
	float Pitch = 0.f;             // grade of the last segment, degrees
	float Bank = 0.f;              // roll of the last knot, degrees, right edge up positive
	int32 TurnKnotsLeft = 0;       // knots left in the current sweep (0 = on a straight)
	float TurnRate = 0.f;          // signed heading change per knot of the current sweep
	int32 StraightKnots = 0;       // knots laid since the last sweep ended
	float LastTurnSign = 0.f;      // direction of the last sweep (0 = none yet)
	bool bSweepFlat = false;       // the current sweep is flat: no bank, outer edge open
	int32 DeferredFlatKnots = 0;   // a flat sweep waiting for the last bank to unwind
	int32 FlatSweepIndex = INDEX_NONE; // its span while it is still being laid
	bool bFlatSweepGrew = false;   // the knot just laid belongs to it
	float LastKnotDistance = 0.f;  // track distance of the knot before the spline's end knot
	bool bLastKnotStraight = false;
	bool bStraightRunRolled = false;
	bool bStraightRunOpen = false;
	int32 OpenStretchIndex = INDEX_NONE;
	FVector3f EndPosition = FVector3f::ZeroVector;

	float FeatureCursor = 0.f;
	TArray<TPair<int32, float>> PendingRamps; // (feature index, distance) waiting for the run-up to settle
	float StraightUntil = 0.f;    // the road is held straight, level and flat up to here: a ramp's landing
	float TargetLength = 0.f;
	float EndZoneTarget = 0.f;    // where the final run-up should begin
	float EndRunUp = 0.f;
	float EndTarget = 0.f;        // where the road should stop: the built run-up start + the run-up
	bool bInEndZone = false;
	bool bComplete = false;
	bool bLoaded = false;

	TArray<float> KnotDistances;
	TArray<FTrackSectionRecord> SectionRecords;
	int32 PlacementsTaken = 0;
};
