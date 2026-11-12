#include "Track/Features/JumpRamp.h"

#include "Components/StaticMeshComponent.h"
#include "Track/Features/TrackFeatureDefinitions.h"

AJumpRamp::AJumpRamp()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AJumpRamp::Configure(const UJumpDefinition* InDefinition, float InStartDistance, float InLateral, float InHalfWidth, float InBoost, bool bInEndRamp, float InRollBonus)
{
	Definition = InDefinition;
	StartDistance = InStartDistance;
	Lateral = InLateral;
	HalfWidth = InHalfWidth;
	Boost = InBoost;
	bEndRamp = bInEndRamp;
	RollBonus = InRollBonus;
}

float AJumpRamp::GetLength() const
{
	return Definition ? Definition->Length : 0.f;
}

float AJumpRamp::Progress(float Distance) const
{
	const float Length = GetLength();
	return Length > 0.f ? FMath::Clamp((Distance - StartDistance) / Length, 0.f, 1.f) : 1.f;
}

float AJumpRamp::HeightAt(float Distance) const
{
	return Definition ? Progress(Distance) * Definition->GetLipHeight() : 0.f;
}

void AJumpRamp::BuildPicture(UStaticMesh* Box, UMaterialInterface* Material)
{
	if (!Definition || !Box) return;

	// Unity's slab and rails, with its (right, up, forward) moved to (forward, right, up).
	const float Length = Definition->Length;
	const float Lip = Definition->GetLipHeight();
	const float Width = HalfWidth * 2.f;
	const float SlopeLength = FMath::Sqrt(Length * Length + Lip * Lip);
	const FQuat Pitch = FRotator(Definition->RampAngle, 0.f, 0.f).Quaternion(); // nose up
	const FVector SlopeCentre(Length * 0.5f, 0.f, Lip * 0.5f);
	const FVector Normal = Pitch.GetUpVector();
	constexpr float Thickness = 1.5f;
	constexpr float RailHeight = 3.f;
	constexpr float RailWidth = 0.8f;

	auto AddBox = [&](const TCHAR* Name, const FVector& Centre, const FVector& Size)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		Part->SetStaticMesh(Box);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); // a picture only
		Part->SetCanEverAffectNavigation(false);
		if (Material) Part->SetMaterial(0, Material);
		Part->SetupAttachment(RootComponent);
		Part->SetRelativeLocationAndRotation(Centre * 100.0, Pitch);
		Part->SetRelativeScale3D(Size); // the engine cube is 1 m
		Part->RegisterComponent();
	};

	AddBox(TEXT("Slab"), SlopeCentre - Normal * (Thickness * 0.5f), FVector(SlopeLength, Width, Thickness));
	AddBox(TEXT("RailL"), SlopeCentre + Pitch.RotateVector(FVector(0.f, -(HalfWidth - RailWidth * 0.5f), RailHeight * 0.5f)), FVector(SlopeLength, RailWidth, RailHeight));
	AddBox(TEXT("RailR"), SlopeCentre + Pitch.RotateVector(FVector(0.f, HalfWidth - RailWidth * 0.5f, RailHeight * 0.5f)), FVector(SlopeLength, RailWidth, RailHeight));
}
