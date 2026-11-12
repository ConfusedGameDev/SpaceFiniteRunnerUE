#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OrbHover.generated.h"

/**
 * Hover for floating orbs (the Unity OrbHover): a small bob round the spawn pose, a spin, and for the
 * rarer tiers a sway ACROSS the track that makes them harder to aim for. The axes are the track's at
 * the spawn pose (its up and its right), so an orb on a loop or in a tube bobs away from the road.
 * GetSwayOffset reports the sway, so the analytic pickup test follows the orb the player sees.
 * Scaled game time: a pause freezes it.
 */
UCLASS(ClassGroup = (Runner), meta = (BlueprintSpawnableComponent))
class RUNNERCORE_API UOrbHoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOrbHoverComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = 0, Units = "m"))
	float BobAmplitude = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = 0, Units = "Hz"))
	float BobFrequency = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover")
	float SpinDegreesPerSecond = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = 0, Units = "m"))
	float SwayAmplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = 0, Units = "Hz"))
	float SwayFrequency = 0.5f;

	void Configure(float InSwayAmplitude, float InSwayFrequency, float InSpin);
	void SetSpin(float InSpin) { SpinDegreesPerSecond = InSpin; }

	/** Takes the owner's current pose as the one it hovers round. Call once it is placed. */
	void Capture();

	/** Metres the orb is swayed across the track right now, right positive. */
	float GetSwayOffset() const { return SwayOffset; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector BasePosition = FVector::ZeroVector;
	FVector BobDirection = FVector::UpVector;
	FVector SwayDirection = FVector::RightVector;
	float Phase = 0.f;
	float SwayOffset = 0.f;
	bool bCaptured = false;
};
