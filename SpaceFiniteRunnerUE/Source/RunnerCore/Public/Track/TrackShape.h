#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TrackShape.generated.h"

class FTrackSection;
class UTrackModel;
struct FRunnerRandom;

/**
 * Base of every track feature kind the generator can place (jumps, loops, tubes). A feature
 * occupies a footprint (FootprintLength, or the length of the section it builds) that pickups keep
 * off when ClaimsFootprint, and an ExclusionAhead no other feature may start in. A feature that
 * owns its pose over its footprint builds a section through CreateSection, at the knot the
 * generator landed on its spot. The generator plays a runtime copy, never the asset.
 */
UCLASS(Abstract, BlueprintType)
class RUNNERCORE_API UTrackFeatureDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Name used in spawned object names and HUD text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	FString DisplayName = TEXT("Feature");

	/** Metres of track this feature occupies from its start, when it builds no section (a section's length wins). */
	virtual float GetFootprintLength() const { return 0.f; }

	/** Metres past the footprint where no other feature may start. */
	virtual float GetExclusionAhead() const { return 0.f; }

	/** True when pickups must keep off the footprint (ramps, loops). A tube wants its orbs. */
	virtual bool ClaimsFootprint() const { return true; }

	/** The section the track is routed through over this feature, or null. Rolls are drawn off Rng in a fixed order. */
	virtual TSharedPtr<FTrackSection> CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const;
};

/** One row of the feature table: what may be placed, how often and how far apart. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackFeatureEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	FString Name = TEXT("Jump");

	/** What the feature is and how it behaves. The generator plays a runtime copy, never this asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	TObjectPtr<UTrackFeatureDefinition> Definition;

	/** Share of every feature roll this entry wins. The table always sums to 100 %. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature", meta = (ClampMin = 0, ClampMax = 100))
	float Probability = 100.f;

	/** Least metres of track between this feature and the next one, on top of its footprint and exclusion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature", meta = (ClampMin = 0, ClampMax = 3000, Units = "m"))
	float MinSpacing = 300.f;

	/** Takeoff boost as a multiple of the game settings' power-up speed boost (1 = a green orb's worth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature", meta = (ClampMin = 0))
	float Multiplier = 1.f;

	/** Tint of the code-built ramp and of its editor markers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.2f);
};

/**
 * Shape of the road itself, as opposed to what sits on it: width, how often it turns, the feature
 * table, elevation, sweeps and banking. The generator runs on a runtime copy; gameplay never
 * writes this asset. All lengths are metres, all angles degrees.
 *
 * Elevation is a pitch walk: every knot the grade drifts by up to MaxGradeStepPerKnot, leans back
 * toward the baseline (BaselinePull), is forced home outside ElevationBand and never exceeds
 * MaxGrade. Turns are sweeps: one direction at one rate (TurnRateRange) for as many knots as the
 * arc (TurnArcRange) needs, then at least MinStraightKnots straight. Banking rolls the road into
 * its sweeps and is zero wherever a feature is coming. A banked sweep always holds the ship;
 * danger is authored by the sweeps left flat (UnbankedSweepChance) and by straights with no walls
 * (OpenStraightChance).
 */
UCLASS(BlueprintType, meta = (UnityType = "TrackShapeSettings"))
class RUNNERCORE_API UTrackShape : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- layout
	/** Full width of the track. Drives the ship's steering clamp, the placement bounds and the road meshes (authored for 60 m, stretched in proportion). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = 10, ClampMax = 120, Units = "m"))
	float TrackWidth = 60.f;

	/** Chance that a straight knot stays straight: 100 % = a dead straight line, 0 % = a sweep at every chance the features leave room for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = 0, ClampMax = 100))
	float Straightness = 100.f;

	// -------------------------------------------------------------- features
	/** One entry per feature kind (jump ramps, loops, tubes). Every feature step draws one entry by probability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Features")
	TArray<FTrackFeatureEntry> FeatureTable;

	/** Metres of track between feature steps, before each entry's own minimum spacing, footprint and exclusion are added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Features", meta = (ClampMin = 100, ClampMax = 5000, Units = "m"))
	FFloatInterval FeatureSpacing = FFloatInterval(600.f, 1200.f);

	// ------------------------------------------------------------- elevation
	/** Let the road rise and fall. Off = the flat track (and no random draws, so a seed reproduces the flat layout exactly). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevation")
	bool bElevationEnabled = true;

	/** How far above or below the start height the road may wander. Inside it the pull leans the grade home; outside it the next knot always heads back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevation", meta = (EditCondition = "bElevationEnabled", ClampMin = 0, ClampMax = 300, Units = "m"))
	float ElevationBand = 60.f;

	/** Steepest grade the road takes. Keep it well under vertical. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevation", meta = (EditCondition = "bElevationEnabled", ClampMin = 0, ClampMax = 20, Units = "deg"))
	float MaxGrade = 6.f;

	/** Most the grade may change from one knot to the next. Knots are 300–420 m apart, so 3° per knot is a swell kilometres long. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevation", meta = (EditCondition = "bElevationEnabled", ClampMin = 0, ClampMax = 10, Units = "deg"))
	float MaxGradeStepPerKnot = 3.f;

	/** How strongly the grade leans back toward the baseline: 0 = a free random walk inside the band, 1 = a full step's worth of pull at the band's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevation", meta = (EditCondition = "bElevationEnabled", ClampMin = 0, ClampMax = 1))
	float BaselinePull = 0.5f;

	// ----------------------------------------------------------------- turns
	/** Heading change per knot during a sweep, rolled per sweep. 30° per knot is a radius of roughly 700 m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 2, ClampMax = 60, Units = "deg"))
	FFloatInterval TurnRateRange = FFloatInterval(15.f, 35.f);

	/** Total heading change of a sweep, rolled per sweep. A sweep lasts arc / rate knots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 10, ClampMax = 180, Units = "deg"))
	FFloatInterval TurnArcRange = FFloatInterval(45.f, 120.f);

	/** Straight knots kept after a sweep before the next may start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 0, ClampMax = 6))
	int32 MinStraightKnots = 1;

	/** Chance the next sweep goes the other way. Keeps the road from spiralling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 0, ClampMax = 1))
	float AlternateTurnChance = 0.7f;

	/** Once the heading has wandered this far from the start direction, the next sweep always heads back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 0, ClampMax = 180, Units = "deg"))
	float MaxHeadingDrift = 150.f;

	/** Chance a sweep is flat instead of banked: no bank, the ship's grip is tested against its speed, and the outer edge has no wall. 0 = every sweep is banked and safe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 0, ClampMax = 1))
	float UnbankedSweepChance = 0.3f;

	/** Chance a straight run has no walls on either side. Rolled once per straight run; only level road well clear of features, landing zones and banked sweeps can be open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turns", meta = (ClampMin = 0, ClampMax = 1))
	float OpenStraightChance = 0.5f;

	// --------------------------------------------------------------- banking
	/** Roll the road into its sweeps. Off = the road stays level through every turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Banking")
	bool bBankEnabled = true;

	/** Steepest bank the road takes (outer edge up). 80 is a wall the ship rides like an oval's banking; keep it under 90. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Banking", meta = (EditCondition = "bBankEnabled", ClampMin = 0, ClampMax = 89, Units = "deg"))
	float MaxBankAngle = 80.f;

	/** Degrees of bank per degree of heading change at a knot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Banking", meta = (EditCondition = "bBankEnabled", ClampMin = 0, ClampMax = 10))
	float BankPerDegreeOfTurn = 4.f;

	/** Most the bank may change from one knot to the next. It also sets how many knots the road needs to unwind before a feature. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Banking", meta = (EditCondition = "bBankEnabled", ClampMin = 0, ClampMax = 90, Units = "deg"))
	float MaxBankStepPerKnot = 45.f;

	/** Extra level road kept before every feature spot, on top of the knots the current bank needs to unwind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Banking", meta = (EditCondition = "bBankEnabled", ClampMin = 0, ClampMax = 3000, Units = "m"))
	float LevelLeadDistance = 200.f;

	// Bands unpacked for the generator, so it never reads Min/Max itself.
	float GetTurnRateMin() const { return FMath::Max(0.1f, TurnRateRange.Min); }
	float GetTurnRateMax() const { return FMath::Max(GetTurnRateMin(), TurnRateRange.Max); }
	float GetTurnArcMin() const { return FMath::Max(1.f, TurnArcRange.Min); }
	float GetTurnArcMax() const { return FMath::Max(GetTurnArcMin(), TurnArcRange.Max); }
	float GetFeatureSpacingMin() const { return FeatureSpacing.Min; }
	float GetFeatureSpacingMax() const { return FMath::Max(FeatureSpacing.Min, FeatureSpacing.Max); }
};
