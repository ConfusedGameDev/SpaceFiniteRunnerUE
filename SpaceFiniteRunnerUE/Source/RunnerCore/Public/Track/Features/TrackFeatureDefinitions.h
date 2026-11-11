#pragma once

#include "CoreMinimal.h"
#include "Track/TrackShape.h"
#include "TrackFeatureDefinitions.generated.h"

/**
 * A jump ramp: a slope narrower than the track at a random lateral. Steer onto it and the ship
 * launches by itself; the arc is authored in track distance (clamp(takeoff speed ×
 * AirDistancePerSpeed, AirDistanceRange) metres down the track), so the longest arc is a knob the
 * generator can keep clear, not a function of speed.
 */
UCLASS(BlueprintType, meta = (UnityType = "JumpDefinition"))
class RUNNERCORE_API UJumpDefinition : public UTrackFeatureDefinition
{
	GENERATED_BODY()

public:
	/** Ramp width as a fraction of the track width, so it keeps reading the same when the width knob moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramp", meta = (ClampMin = 0.05, ClampMax = 1))
	float WidthFraction = 0.25f;

	/** Run-up length: the slope the ship rides before the lip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramp", meta = (ClampMin = 10, ClampMax = 200, Units = "m"))
	float Length = 60.f;

	/** Slope of the ramp. The lip height is length × tan(angle), and the ship leaves the lip at this angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramp", meta = (ClampMin = 5, ClampMax = 45, Units = "deg"))
	float RampAngle = 20.f;

	/** How far inside the ramp's edge the ship's centre must be to enter it (half the ship's width). Closer to the edge is a side hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramp", meta = (ClampMin = 0, ClampMax = 10, Units = "m"))
	float EntryMargin = 2.5f;

	/** Metres of flight per m/s of takeoff speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arc", meta = (ClampMin = 0.05, ClampMax = 3))
	float AirDistancePerSpeed = 0.6f;

	/** Shortest and longest arc, in metres of track. The upper end is the exclusion the generator keeps clear ahead of every ramp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arc", meta = (ClampMin = 20, ClampMax = 2000, Units = "m"))
	FFloatInterval AirDistanceRange = FFloatInterval(80.f, 600.f);

	/** Straight, level, flat road kept beyond the longest possible landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arc", meta = (ClampMin = 0, ClampMax = 600, Units = "m"))
	float LandingClearance = 150.f;

	/**
	 * The strongest jump any ship can make (the Unity Store's top jump-strength level, 1.5). Landing
	 * zones are sized for it, so a seed or a saved track is one road for everyone (R1.10).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arc", meta = (ClampMin = 1, ClampMax = 3))
	float DesignJumpStrength = 1.5f;

	/** Steering and dash authority while airborne, as a fraction of grounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = 0, ClampMax = 1))
	float AirControlFactor = 0.5f;

	/** Fraction of the current speed lost when the ship hits the ramp from the side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Side Hit", meta = (ClampMin = 0, ClampMax = 1))
	float SideHitSpeedLoss = 0.15f;

	/** Height of the lip above the flight line. */
	float GetLipHeight() const { return Length * FMath::Tan(FMath::DegreesToRadians(RampAngle)); }
	/** Rise per metre of track. */
	float GetSlope() const { return FMath::Tan(FMath::DegreesToRadians(RampAngle)); }
	/** Arc length in metres of track for a takeoff at Speed m/s. */
	float AirDistanceFor(float Speed) const { return FMath::Clamp(Speed * AirDistancePerSpeed, AirDistanceRange.Min, AirDistanceRange.Max); }
	/** The longest arc a ship of JumpStrength can fly off this ramp. */
	float MaxAirDistance(float JumpStrength) const { return AirDistanceRange.Max * FMath::Max(0.f, JumpStrength); }

	virtual float GetFootprintLength() const override { return Length; }
	/** The arc cap at strength 1; the generator widens it by the design strength and the landing clearance. */
	virtual float GetExclusionAhead() const override { return AirDistanceRange.Max; }
};

/**
 * A mandatory vertical loop the whole track width wide, inserted into the track's distance
 * (FLoopSection). Its entry speed rule lives on the game settings; a loop is only placed where the
 * ship would arrive fast enough (GateHeadroom over the demand).
 */
UCLASS(BlueprintType, meta = (UnityType = "LoopDefinition"))
class RUNNERCORE_API ULoopDefinition : public UTrackFeatureDefinition
{
	GENERATED_BODY()

public:
	/** Radius of the loop. At 100 m the loop is 628 m of track. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop", meta = (ClampMin = 40, ClampMax = 250, Units = "m"))
	float Radius = 100.f;

	/** Plain track kept clear after the exit before the next feature may start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop", meta = (ClampMin = 0, ClampMax = 1000, Units = "m"))
	float ExitClearance = 200.f;

	/** How far sideways the exit lands from the entry (a corkscrew). Side is random. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop|Variation", meta = (ClampMin = 0, ClampMax = 600, Units = "m"))
	FFloatInterval LateralDriftRange = FFloatInterval(0.f, 240.f);

	/** How far ahead of the entry the exit lands (an elongated loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop|Variation", meta = (ClampMin = 0, ClampMax = 1000, Units = "m"))
	FFloatInterval ForwardCarryRange = FFloatInterval(0.f, 300.f);

	/** Heading change between entry and exit, about the entry's up. Side is random. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop|Variation", meta = (ClampMin = 0, ClampMax = 60, Units = "deg"))
	FFloatInterval ExitYawRange = FFloatInterval(0.f, 30.f);

	/** Full turns per loop (min X, max Y). A failed loop still drops from the top of the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop|Variation", meta = (ClampMin = 1, ClampMax = 3))
	FIntPoint TurnsRange = FIntPoint(1, 1);

	/** Fake gravity of the drop from the top of a failed loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fall", meta = (ClampMin = 20, ClampMax = 400))
	float FallGravity = 120.f;

	/** Fraction of the current speed lost on dropping off the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fall", meta = (ClampMin = 0, ClampMax = 1))
	float FallSpeedLoss = 0.4f;

	/** Gate colour while the ship is fast enough. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FLinearColor PassColor = FLinearColor(0.2f, 1.f, 0.45f);

	/** Gate colour while the ship is too slow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FLinearColor FailColor = FLinearColor(1.f, 0.2f, 0.15f);

	/** Height of the required-speed number above the flight line at the mouth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = 5, ClampMax = 150, Units = "m"))
	float LabelHeight = 30.f;

	/** Character size of the required-speed number (roughly metres per character). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = 0.5, ClampMax = 20))
	float LabelSize = 5.f;

	/** How far ahead of the loop its required-speed number is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = 50, ClampMax = 4000, Units = "m"))
	float LabelLeadMeters = 1800.f;

	/** A loop is only placed when the predicted speed at the gate clears the demand by this fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = 0, ClampMax = 0.5))
	float GateHeadroom = 0.1f;

	float GetDriftMin() const { return FMath::Max(0.f, LateralDriftRange.Min); }
	float GetDriftMax() const { return FMath::Max(GetDriftMin(), LateralDriftRange.Max); }
	float GetCarryMin() const { return FMath::Max(0.f, ForwardCarryRange.Min); }
	float GetCarryMax() const { return FMath::Max(GetCarryMin(), ForwardCarryRange.Max); }
	float GetYawMin() const { return FMath::Max(0.f, ExitYawRange.Min); }
	float GetYawMax() const { return FMath::Max(GetYawMin(), ExitYawRange.Max); }
	int32 GetTurnsMin() const { return FMath::Max(1, TurnsRange.X); }
	int32 GetTurnsMax() const { return FMath::Max(GetTurnsMin(), TurnsRange.Y); }
	float GetCircumference() const { return 2.f * UE_PI * Radius; }

	virtual float GetFootprintLength() const override { return GetCircumference(); }
	virtual float GetExclusionAhead() const override { return ExitClearance; }
	virtual TSharedPtr<FTrackSection> CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const override;
};

/**
 * A cylinder section: the road curls into a pipe the ship runs round the outside of (FTubeSection).
 * One asset per band kind (±90°, a ±180° full tube).
 */
UCLASS(BlueprintType, meta = (UnityType = "TubeDefinition"))
class RUNNERCORE_API UTubeDefinition : public UTrackFeatureDefinition
{
	GENERATED_BODY()

public:
	/** Radius of the pipe. 60 m is a 377 m circumference. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tube", meta = (ClampMin = 20, ClampMax = 150, Units = "m"))
	float Radius = 60.f;

	/** Half the steering band round the pipe. 90 = hang off either side; 180 = a full tube with no clamp once curled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tube", meta = (ClampMin = 15, ClampMax = 180, Units = "deg"))
	float BandDegrees = 90.f;

	/** Where the band is centred, degrees from the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tube", meta = (ClampMin = -180, ClampMax = 180, Units = "deg"))
	float CentreDegrees = 0.f;

	/** Steering authority on the pipe as a multiple of the road's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tube", meta = (ClampMin = 0.5, ClampMax = 6))
	float SteeringFactor = 3.f;

	/** Section length rolled per instance, metres of track. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Length", meta = (ClampMin = 200, ClampMax = 12000, Units = "m"))
	FFloatInterval LengthRange = FFloatInterval(3000.f, 7000.f);

	/** Metres at each end over which the flat road curls into the pipe and back out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Length", meta = (ClampMin = 20, ClampMax = 500, Units = "m"))
	float CurlLength = 150.f;

	/** Metres before the curl-out over which the ship is steered back to the band's centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Length", meta = (ClampMin = 100, ClampMax = 1500, Units = "m"))
	float ReturnLength = 400.f;

	/** Plain track kept clear after the exit before the next feature may start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Length", meta = (ClampMin = 0, ClampMax = 1000, Units = "m"))
	float ExitClearance = 150.f;

	virtual float GetFootprintLength() const override { return LengthRange.Max; }
	virtual float GetExclusionAhead() const override { return ExitClearance; }
	virtual bool ClaimsFootprint() const override { return false; }
	virtual TSharedPtr<FTrackSection> CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const override;
};
