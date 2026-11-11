#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PadDefinition.generated.h"

/** What a speed pad or orb does when a ship takes it. Tiers override the delta and colour per instance; this asset is never written. */
UCLASS(BlueprintType, meta = (UnityType = "PadDefinition"))
class RUNNERCORE_API UPadDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	FString DisplayName = TEXT("Pad");

	/** Speed change applied when a ship passes over the pad, m/s. Positive = boost, negative = brake. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (Units = "m/s"))
	float SpeedDelta = 10.f;

	/** Tint applied to the pad so the type is readable at speed (as authored in Unity, sRGB). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	FLinearColor Color = FLinearColor::Green;

	/** Scale applied to the generator's base pad size. Boosts are small (0.3) and must be aimed for; brakes are large. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0.05))
	float SizeMultiplier = 1.f;

	/** Spawn as a hovering orb on the flight line instead of a flat pad on the road. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	bool bFloatingOrb = false;
};
