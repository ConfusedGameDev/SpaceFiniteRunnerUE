#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrackContracts.h"
#include "RepairOrb.generated.h"

class UMaterialInterface;
class UOrbHoverComponent;
class USphereComponent;
class UStaticMesh;

/**
 * A repair orb (the Unity RepairOrb): flown through, it gives back part of the hull. Deliberately not
 * an ITrackPickup and never in the UPickupRegistry, so the patrol neither seeks nor takes it and it
 * gives no speed impulse. Only the ship's own step sweep finds it, by its query-only sphere tagged
 * ShipPickup (M3); taking it is M4/M5.
 */
UCLASS(NotBlueprintable)
class RUNNERCORE_API ARepairOrb : public AActor, public ITrackPositioned
{
	GENERATED_BODY()

public:
	ARepairOrb();

	/** Tag on the pickup sphere the ship's sweep looks for. */
	static const FName ShipPickupTag;

	/** Model at Diameter (the imported MedPack is modelled at 1 m), else the code-built shell and cross. */
	void Configure(float Distance, float Diameter, float Spin, UStaticMesh* Model, UMaterialInterface* ShellMaterial, UMaterialInterface* CrossMaterial);

	bool IsAvailable() const { return !bTaken; }
	void MarkTaken();

	/** How far the hover carries the orb either side of its centre: the spawner lifts it by this. */
	float GetBobAmplitude() const;

	virtual float GetTrackDistance() const override { return TrackDistance; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Picture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UOrbHoverComponent> Hover;

private:
	float TrackDistance = 0.f;
	bool bTaken = false;
};
