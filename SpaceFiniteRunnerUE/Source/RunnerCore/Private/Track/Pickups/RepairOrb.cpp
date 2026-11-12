#include "Track/Pickups/RepairOrb.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Track/Pickups/OrbHover.h"

const FName ARepairOrb::ShipPickupTag(TEXT("ShipPickup"));

ARepairOrb::ARepairOrb()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetMobility(EComponentMobility::Movable);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(false);
	Trigger->ComponentTags.Add(ShipPickupTag);
	RootComponent = Trigger;

	Picture = CreateDefaultSubobject<USceneComponent>(TEXT("Picture"));
	Picture->SetMobility(EComponentMobility::Movable);
	Picture->SetupAttachment(Trigger);

	Hover = CreateDefaultSubobject<UOrbHoverComponent>(TEXT("Hover"));
}

float ARepairOrb::GetBobAmplitude() const
{
	return Hover ? Hover->BobAmplitude : 0.f;
}

void ARepairOrb::Configure(float Distance, float Diameter, float Spin, UStaticMesh* Model, UMaterialInterface* ShellMaterial, UMaterialInterface* CrossMaterial)
{
	TrackDistance = Distance;
	Trigger->SetSphereRadius(Diameter * 0.5f * 100.f);
	Picture->SetRelativeScale3D(FVector(Diameter));

	auto AddPart = [this](UStaticMesh* Mesh, const FVector& Scale, UMaterialInterface* Material)
	{
		if (!Mesh) return;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the root's sphere is the pickup: a solid part would be flown into
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Picture);
		C->SetRelativeScale3D(Scale);
		if (Material) C->SetMaterial(0, Material);
		AddInstanceComponent(C);
		C->RegisterComponent();
	};

	if (Model)
	{
		AddPart(Model, FVector::OneVector, nullptr);
	}
	else
	{
		// The shell round a cross of three arms (the third because it spins: the cross reads from any side).
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AddPart(Sphere, FVector::OneVector, ShellMaterial);
		AddPart(Cube, FVector(0.2, 0.2, 0.6), CrossMaterial);
		AddPart(Cube, FVector(0.2, 0.6, 0.2), CrossMaterial);
		AddPart(Cube, FVector(0.6, 0.2, 0.2), CrossMaterial);
	}

	Hover->SetSpin(Spin);
	Hover->Capture();
}

void ARepairOrb::MarkTaken()
{
	if (bTaken) return;
	bTaken = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Hover->SetComponentTickEnabled(false);
}
