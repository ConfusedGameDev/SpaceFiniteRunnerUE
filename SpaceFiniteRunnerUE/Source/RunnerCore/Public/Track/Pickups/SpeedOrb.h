#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PickupContracts.h"
#include "TrackContracts.h"
#include "SpeedOrb.generated.h"

class UMaterialInterface;
class UOrbHoverComponent;
class UPadDefinition;
class URunnerPrefabLayout;
class UStaticMeshComponent;
struct FPadSpawnEntry;

/**
 * A boost orb or flat pad (the Unity SpeedPad): it changes the speed of a ship passing through it.
 * Detection is analytic: the spawner hands it its track-space spot, it sits in the UPickupRegistry
 * while it is available, and the ship's sweep finds it (M3/M5). Whatever the picture is, it has no
 * collision. A boost orb is used up when taken; a flat pad stays painted on the road but bites once.
 */
UCLASS(NotBlueprintable)
class RUNNERCORE_API ASpeedOrb : public AActor, public ITrackPickup, public ITrackPositioned
{
	GENERATED_BODY()

public:
	ASpeedOrb();

	/**
	 * Shapes it: the entry's look (its prefab layout, else a code-built sphere or slab in Material),
	 * its track spot (Height above the flight line: the air lane) and its pickup volume (half extents,
	 * along / across / up, metres). SpeedDelta is the boost it gives (a brake's own delta for a brake).
	 */
	void Configure(const FPadSpawnEntry& Entry, int32 TierIndex, float InSpeedDelta, float Distance, float Lateral, float Height,
		const FVector3f& HalfExtents, const FVector3f& PadSize, UMaterialInterface* Material);

	const UPadDefinition* GetDefinition() const { return Definition; }
	int32 GetTierIndex() const { return TierIndex; }
	const FString& GetTierName() const { return TierName; }
	float GetSpeedDelta() const { return SpeedDelta; }
	FLinearColor GetColor() const { return Color; }
	/** A floating orb that speeds a ship up: what the patrol goes after. */
	bool IsBoostOrb() const;

	/** Used up, nothing announced; returns the delta it carried (0 when already gone). M5 adds the ship's Collect. */
	float Take();

	// ITrackPickup
	virtual float GetPickupDistance() const override { return TrackDistance; }
	virtual float GetPickupLateral() const override;
	virtual float GetPickupHeight() const override { return TrackHeight; }
	virtual FVector3f GetPickupHalfExtents() const override { return HalfExtents; }
	virtual bool IsPickupAvailable() const override;

	// ITrackPositioned
	virtual float GetTrackDistance() const override { return TrackDistance; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Scaled to the prefab root's scale times the pad's size; the meshes hang under it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Picture;

	/** Added only to floating orbs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UOrbHoverComponent> Hover;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<const UPadDefinition> Definition;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	FString TierName;
	FLinearColor Color = FLinearColor::White;
	int32 TierIndex = INDEX_NONE;
	float SpeedDelta = 0.f;
	float TrackDistance = 0.f;
	float TrackLateral = 0.f;
	float TrackHeight = 0.f;
	FVector3f HalfExtents = FVector3f::ZeroVector;
	bool bPlaced = false;
	bool bTaken = false;
};
