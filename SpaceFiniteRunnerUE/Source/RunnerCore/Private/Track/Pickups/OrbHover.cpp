#include "Track/Pickups/OrbHover.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

UOrbHoverComponent::UOrbHoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UOrbHoverComponent::Configure(float InSwayAmplitude, float InSwayFrequency, float InSpin)
{
	SwayAmplitude = InSwayAmplitude;
	SwayFrequency = InSwayFrequency;
	SpinDegreesPerSecond = InSpin;
}

void UOrbHoverComponent::Capture()
{
	const AActor* Owner = GetOwner();
	if (!Owner) return;
	BasePosition = Owner->GetActorLocation();
	// The spin turns the actor, so the track axes are taken once.
	BobDirection = Owner->GetActorUpVector();
	SwayDirection = Owner->GetActorRightVector();
	// Desync neighbouring orbs so the track doesn't bob in unison (Unity: (x + z) * 0.7, world metres).
	Phase = static_cast<float>((BasePosition.X + BasePosition.Y) * 0.01 * 0.7);
	bCaptured = true;
}

void UOrbHoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World) return;
	if (!bCaptured) Capture();

	const float Time = World->GetTimeSeconds() + Phase;
	const float Bob = FMath::Sin(Time * BobFrequency * UE_TWO_PI) * BobAmplitude;
	SwayOffset = FMath::Sin(Time * SwayFrequency * UE_TWO_PI) * SwayAmplitude;
	Owner->SetActorLocation(BasePosition + (BobDirection * Bob + SwayDirection * SwayOffset) * 100.0);
	if (SpinDegreesPerSecond != 0.f)
	{
		Owner->AddActorWorldRotation(FQuat(BobDirection, FMath::DegreesToRadians(SpinDegreesPerSecond * DeltaTime)));
	}
}
