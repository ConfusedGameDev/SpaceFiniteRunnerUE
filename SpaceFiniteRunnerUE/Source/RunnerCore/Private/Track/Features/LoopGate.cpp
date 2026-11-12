#include "Track/Features/LoopGate.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/TrackSection.h"

namespace
{
	const FName BaseColorParam(TEXT("BaseColor"));
	const FName EmissionColorParam(TEXT("EmissionColor"));
}

ALoopGate::ALoopGate()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void ALoopGate::Configure(const ULoopDefinition* InDefinition, const FLoopSection* InSection, float InRequiredSpeed)
{
	Definition = InDefinition;
	Section = InSection;
	RequiredSpeed = InRequiredSpeed;
	StartDistance = InSection ? InSection->GetStartDistance() : 0.f;
	EndDistance = InSection ? InSection->GetEndDistance() : 0.f;
}

void ALoopGate::BuildPicture(UStaticMesh* Box, UMaterialInterface* Material, float TrackHalfWidth, float SpeedDisplayMultiplier)
{
	if (!Definition) return;
	GateMaterial = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;

	const float Half = TrackHalfWidth + 4.f;
	constexpr float PostWidth = 3.f;
	const float PostHeight = FMath::Clamp(TrackHalfWidth * 0.6f, 20.f, 60.f);
	const float LabelHeight = FMath::Max(Definition->LabelHeight, PostHeight + PostWidth);

	auto AddBox = [&](const TCHAR* Name, const FVector& Centre, const FVector& Size)
	{
		if (!Box) return;
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		Part->SetStaticMesh(Box);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); // a picture only
		Part->SetCanEverAffectNavigation(false);
		if (GateMaterial) Part->SetMaterial(0, GateMaterial);
		Part->SetupAttachment(RootComponent);
		Part->SetRelativeLocation(Centre * 100.0);
		Part->SetRelativeScale3D(Size); // the engine cube is 1 m
		Part->RegisterComponent();
	};

	AddBox(TEXT("PostL"), FVector(0.f, -Half, PostHeight * 0.5f - 1.f), FVector(PostWidth, PostWidth, PostHeight));
	AddBox(TEXT("PostR"), FVector(0.f, Half, PostHeight * 0.5f - 1.f), FVector(PostWidth, PostWidth, PostHeight));
	AddBox(TEXT("Crossbar"), FVector(0.f, 0.f, PostHeight - 1.f), FVector(PostWidth, Half * 2.f + PostWidth, PostWidth));

	// The number at the mouth: fixed above the gate, the required speed in the display unit only (the
	// colour already says pass or fail). A number, so no String Table entry.
	Label = NewObject<UTextRenderComponent>(this, TEXT("RequiredSpeedLabel"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight * 100.f));
	Label->SetRelativeRotation(FRotator(0.f, 180.f, 0.f)); // text reads toward +X; the ship looks along +X
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(Definition->LabelSize * 480.f); // Unity: font size 48 at the label's character size
	Label->SetText(FText::AsNumber(FMath::RoundToInt(RequiredSpeed * SpeedDisplayMultiplier)));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->RegisterComponent();
	Label->SetVisibility(false); // shown inside the alert lead

	SetGateColor(false);
}

void ALoopGate::SetGateColor(bool bPass)
{
	const int8 State = bPass ? 1 : 0;
	if (GateState == State || !Definition) return;
	GateState = State;
	const FLinearColor Color = bPass ? Definition->PassColor : Definition->FailColor;
	if (GateMaterial)
	{
		GateMaterial->SetVectorParameterValue(BaseColorParam, Color);
		GateMaterial->SetVectorParameterValue(EmissionColorParam, Color);
	}
	if (Label) Label->SetTextRenderColor(Color.ToFColor(true));
}

void ALoopGate::SetLabelVisible(bool bVisible)
{
	if (Label && Label->IsVisible() != bVisible) Label->SetVisibility(bVisible);
}

bool ALoopGate::IsLabelVisible() const
{
	return Label && Label->IsVisible();
}
