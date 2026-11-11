#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TrackRevealSettings.generated.h"

/**
 * Where along the road ahead the track assembles itself, and how a piece flies in. Distances are
 * metres of track ahead of the reveal's reference (the ship, or a race's leader), never seconds: at
 * Light Speed a time band would sit wholly inside the fog, while a distance band just plays faster.
 * Read live, never copied; gameplay never writes it.
 */
UCLASS(BlueprintType, meta = (UnityType = "TrackRevealSettings"))
class RUNNERCORE_API UTrackRevealSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Master switch. Off, the whole track stands assembled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reveal")
	bool bRevealEnabled = true;

	/** Metres ahead of the reference where a piece starts rising. Keep it inside the distance fog's end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reveal", meta = (ClampMin = 50, ClampMax = 1500, Units = "m"))
	float StartAhead = 450.f;

	/** Metres a piece takes to seat once the front passes it. Seated at Start Ahead - Band (+ its stagger). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reveal", meta = (ClampMin = 10, ClampMax = 1000, Units = "m"))
	float Band = 300.f;

	/** Random extra delay per piece, metres: pieces land one by one instead of as a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reveal", meta = (ClampMin = 0, ClampMax = 300, Units = "m"))
	float Stagger = 60.f;

	/** While the reference is slower than this, the front creeps ahead on its own until the visible road is seated (Start Ahead + Band). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reveal", meta = (ClampMin = 0, ClampMax = 1000, Units = "m/s"))
	float CreepSpeed = 150.f;

	/** How far below its rest a piece starts, metres along its own up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = 0, ClampMax = 200, Units = "m"))
	float Drop = 40.f;

	/** How far a piece starts tipped about its own forward-crossing axis, degrees (the side is random per piece). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float TiltDegrees = 25.f;

	/** Scale a piece starts at (1 = full size all the way). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = 0, ClampMax = 1))
	float StartScale = 0.3f;

	/** Overshoot of the landing: 0 = a plain ease out, higher = the piece rises past its rest and settles back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = 0, ClampMax = 3))
	float Overshoot = 1.2f;
};
