#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PickupContracts.generated.h"

/**
 * Something on the track a body picks up or runs into, described in TRACK SPACE (the body's own
 * coordinates), so detection is a comparison of numbers and never a physics overlap (invariant 4).
 * Boost orbs and laser gates are; the repair orb is not (only the ship's own sweep takes it).
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTrackPickup : public UInterface
{
	GENERATED_BODY()
};

class RUNNERCONTRACTS_API ITrackPickup
{
	GENERATED_BODY()

public:
	/** Metres from the track start, at the volume's centre. */
	virtual float GetPickupDistance() const = 0;
	/** Metres across the track right now (a swaying orb moves it). */
	virtual float GetPickupLateral() const = 0;
	/** Height of the centre above the flight line, metres (0 on the ground lane). */
	virtual float GetPickupHeight() const = 0;
	/** Half size of the volume, metres, in Unreal's track order: X along the track, Y across, Z up. */
	virtual FVector3f GetPickupHalfExtents() const = 0;
	/** False once taken (or switched off): sweeps skip it. */
	virtual bool IsPickupAvailable() const = 0;
};
