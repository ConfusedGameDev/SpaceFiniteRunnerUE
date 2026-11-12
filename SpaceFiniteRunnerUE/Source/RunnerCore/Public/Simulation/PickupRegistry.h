#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PickupRegistry.generated.h"

class ITrackPickup;

/**
 * Every pad, orb and laser gate currently on the track (the Unity PickupRegistry), registered with its
 * track-space spot when it is built and dropped when it is culled, taken or destroyed. A body sweeps
 * it each step over the distance it just covered, which is what makes pickups tunnel-proof at any
 * speed, and the patrol and the traffic read it to see what lies ahead. A plain list scanned
 * linearly: a streamed track holds a hundred or so live entries. One per world, so a sandbox and a
 * PIE session never share one.
 */
UCLASS()
class RUNNERCORE_API UPickupRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPickupRegistry* Get(const UObject* WorldContext);

	/** Pickup must implement ITrackPickup. Registering twice is a no-op. */
	void Register(UObject* Pickup);
	void Unregister(UObject* Pickup);

	/** Live entries, in registration order (null-free). */
	void GetAll(TArray<ITrackPickup*>& Out) const;
	int32 Num() const;

	/**
	 * Every available pickup whose volume a body touched while covering FromDistance → ToDistance at
	 * this lateral and height, with the body's own half width and half height added on. LateralWrap
	 * > 0 compares laterals round a full tube of that circumference.
	 */
	void Sweep(float FromDistance, float ToDistance, float Lateral, float Height, float ReachLateral, float ReachHeight,
		float LateralWrap, TArray<ITrackPickup*>& Out) const;

private:
	TArray<TWeakObjectPtr<UObject>> Entries;
};
