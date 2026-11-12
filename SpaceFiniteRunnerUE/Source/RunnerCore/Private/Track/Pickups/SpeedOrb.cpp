#include "Track/Pickups/SpeedOrb.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Prefabs/RunnerPrefabLayout.h"
#include "Simulation/PickupRegistry.h"
#include "Track/Pickups/OrbHover.h"
#include "Track/RunnerColor.h"
#include "Track/Spawning/PadDefinition.h"
#include "Track/Spawning/TrackSpawner.h"

ASpeedOrb::ASpeedOrb()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
	Picture = CreateDefaultSubobject<USceneComponent>(TEXT("Picture"));
	Picture->SetMobility(EComponentMobility::Movable);
	Picture->SetupAttachment(Root);
}

void ASpeedOrb::Configure(const FPadSpawnEntry& Entry, int32 InTierIndex, float InSpeedDelta, float Distance, float Lateral, float Height,
	const FVector3f& InHalfExtents, const FVector3f& PadSize, UMaterialInterface* Material)
{
	Definition = Entry.Definition;
	TierIndex = InTierIndex;
	TierName = Entry.Name;
	Color = Entry.Color;
	SpeedDelta = InSpeedDelta;
	TrackDistance = Distance;
	TrackLateral = Lateral;
	TrackHeight = Height;
	HalfExtents = InHalfExtents;

	const bool bOrb = Definition && Definition->bFloatingOrb;
	const float Size = Definition ? Definition->SizeMultiplier : 1.f;

	if (const URunnerPrefabLayout* Layout = Entry.Prefab)
	{
		// The designer's look: it keeps its own materials, only the size scales it.
		Picture->SetRelativeScale3D(Layout->RootScale * Size);
		TArray<UStaticMeshComponent*> Built;
		RunnerPrefab::Assemble(*Layout, *Picture, Built);
		Meshes.Append(Built);
	}
	else
	{
		// Code-built: a sphere on the flight line, small and to be aimed for, or the whole pad slab.
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, bOrb ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cube.Cube"));
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, TEXT("Pad"), RF_Transient);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Picture);
		// Unreal's (along, across, up) from the pad size (width, height, length).
		C->SetRelativeScale3D(bOrb ? FVector(PadSize.X * Size) : FVector(PadSize.Z, PadSize.X, PadSize.Y) * Size);
		if (Material) C->SetMaterial(0, Material);
		AddInstanceComponent(C);
		C->RegisterComponent();
		Meshes.Add(C);
	}

	// Every renderer wears the tier's colour, as the Unity pad tinted them all (materials without a base colour ignore it).
	const FLinearColor Tint = RunnerColor::FromAuthored(Color);
	for (UStaticMeshComponent* C : Meshes)
	{
		C->SetVectorParameterValueOnMaterials(RunnerColor::BaseColorParam, FVector(Tint));
	}

	if (bOrb)
	{
		Hover = NewObject<UOrbHoverComponent>(this, TEXT("Hover"));
		Hover->Configure(Entry.SwayAmplitude, Entry.SwayFrequency, Entry.SpinDegreesPerSecond);
		AddInstanceComponent(Hover);
		Hover->RegisterComponent();
		Hover->Capture();
	}

	bPlaced = true;
	if (UPickupRegistry* Registry = UPickupRegistry::Get(this)) Registry->Register(this);
}

bool ASpeedOrb::IsBoostOrb() const
{
	return Definition && Definition->bFloatingOrb && SpeedDelta > 0.f;
}

float ASpeedOrb::GetPickupLateral() const
{
	return TrackLateral + (Hover ? Hover->GetSwayOffset() : 0.f);
}

bool ASpeedOrb::IsPickupAvailable() const
{
	return bPlaced && !bTaken && Definition != nullptr;
}

float ASpeedOrb::Take()
{
	if (!IsPickupAvailable()) return 0.f;
	bTaken = true;
	if (Definition->bFloatingOrb)
	{
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		if (Hover) Hover->SetComponentTickEnabled(false);
		if (UPickupRegistry* Registry = UPickupRegistry::Get(this)) Registry->Unregister(this);
	}
	return SpeedDelta;
}

void ASpeedOrb::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UPickupRegistry* Registry = UPickupRegistry::Get(this)) Registry->Unregister(this);
	Super::EndPlay(Reason);
}
