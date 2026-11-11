#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TrackContracts.generated.h"

/** What the track streams round: usually the player ship. Distance is the authoritative coordinate. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStreamFocus : public UInterface
{
	GENERATED_BODY()
};

class RUNNERCONTRACTS_API IStreamFocus
{
	GENERATED_BODY()

public:
	/** Track distance of the focus, metres. */
	virtual float GetStreamDistance() const = 0;
	/** Forward speed of the focus, m/s. */
	virtual float GetStreamSpeed() const = 0;
};

/** Anything that stands at a track distance (props the reveal and the minimap read). */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTrackPositioned : public UInterface
{
	GENERATED_BODY()
};

class RUNNERCONTRACTS_API ITrackPositioned
{
	GENERATED_BODY()

public:
	virtual float GetTrackDistance() const = 0;
};

/**
 * The run's rules as the track needs them (the game mode's settings, a level's overrides). The track
 * reads them through this, never the game mode's class, so it boots alone in its sandbox with none.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTrackRunRules : public UInterface
{
	GENERATED_BODY()
};

class RUNNERCONTRACTS_API ITrackRunRules
{
	GENERATED_BODY()

public:
	/** Target length of a generated track, metres. */
	virtual float GetTrackLengthMeters() const = 0;
	/** Straight, feature-free run-up before the end ramps, metres. */
	virtual float GetEndRunUpMeters() const = 0;
	/** Gap between the three end ramps, metres. */
	virtual float GetEndRampGapMeters() const = 0;
	/** Gap between the outer end ramps and the road edge, metres. */
	virtual float GetEndRampSideGapMeters() const = 0;
	/** The shared base a boost orb's multiplier scales, m/s. */
	virtual float GetPowerUpSpeedBoost() const = 0;
	/** Height of the air lane above the flight line, metres. */
	virtual float GetAirLaneHeight() const = 0;
	/** Off = no hull, so no repair orbs. */
	virtual bool IsHullEnabled() const = 0;
	/** m/s to the displayed unit (× 3.6 for km/h). */
	virtual float GetSpeedDisplayMultiplier() const = 0;
	/** The speed a loop at this distance needs, m/s. */
	virtual float GetLoopRequiredSpeed(float Distance) const = 0;
	/** The level's saved track, if it plays one (else null: generate). */
	virtual UObject* GetAuthoredTrack() const = 0;
};
