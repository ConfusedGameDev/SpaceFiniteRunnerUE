#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoostMeterSettings.generated.h"

/**
 * The tunables of the held boost, and nothing else: it keeps its own asset so it never grows the
 * game settings. Read live; gameplay never writes it.
 */
UCLASS(BlueprintType, meta = (UnityType = "BoostMeterSettings"))
class RUNNERCORE_API UBoostMeterSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr float KmhPerMs = 3.6f;

	/** Master switch. Off, Boost does nothing and the HUD hides the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boost")
	bool bMeterEnabled = true;

	/** Extra thrust while Boost is held, in km/h per second. It can push past cruise; above cruise the ship's passive bleed still pulls against it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = 0, ClampMax = 1500))
	float PotenceKmhPerSecond = 300.f;

	/** Share of the meter a second of boosting uses. 0.25 = four seconds from full to empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meter", meta = (ClampMin = 0.05, ClampMax = 2))
	float DrainPerSecond = 0.25f;

	/** Seconds Boost must go unpressed before the meter starts refilling. Every press, an empty one too, starts it again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meter", meta = (ClampMin = 0, ClampMax = 15, Units = "s"))
	float CooldownSeconds = 5.f;

	/** Share of the meter refilled per second once the cooldown is over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meter", meta = (ClampMin = 0.05, ClampMax = 2))
	float RefillPerSecond = 0.2f;

	/** Rumble for a press on an empty meter: X = low motor, Y = high motor, Z = seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	FVector EmptyPressRumble = FVector(0.5, 0.2, 0.15);
};
