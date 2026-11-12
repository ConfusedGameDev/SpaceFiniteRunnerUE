#include "Simulation/PickupRegistry.h"

#include "Engine/World.h"
#include "PickupContracts.h"

UPickupRegistry* UPickupRegistry::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UPickupRegistry>() : nullptr;
}

void UPickupRegistry::Register(UObject* Pickup)
{
	if (!Pickup || !Pickup->Implements<UTrackPickup>()) return;
	Entries.AddUnique(Pickup);
}

void UPickupRegistry::Unregister(UObject* Pickup)
{
	Entries.RemoveAll([Pickup](const TWeakObjectPtr<UObject>& E) { return !E.IsValid() || E.Get() == Pickup; });
}

void UPickupRegistry::GetAll(TArray<ITrackPickup*>& Out) const
{
	Out.Reset();
	for (const TWeakObjectPtr<UObject>& E : Entries)
	{
		if (ITrackPickup* P = Cast<ITrackPickup>(E.Get())) Out.Add(P);
	}
}

int32 UPickupRegistry::Num() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UObject>& E : Entries) N += E.IsValid() ? 1 : 0;
	return N;
}

void UPickupRegistry::Sweep(float FromDistance, float ToDistance, float Lateral, float Height, float ReachLateral, float ReachHeight,
	float LateralWrap, TArray<ITrackPickup*>& Out) const
{
	for (const TWeakObjectPtr<UObject>& E : Entries)
	{
		ITrackPickup* P = Cast<ITrackPickup>(E.Get());
		if (!P || !P->IsPickupAvailable()) continue;

		const FVector3f Half = P->GetPickupHalfExtents();
		const float D = P->GetPickupDistance();
		if (D + Half.X < FromDistance || D - Half.X > ToDistance) continue;

		float Across = P->GetPickupLateral() - Lateral;
		if (LateralWrap > 0.f)
		{
			// Mathf.Repeat(across + wrap/2, wrap) - wrap/2
			const float Shifted = Across + LateralWrap * 0.5f;
			Across = Shifted - FMath::FloorToFloat(Shifted / LateralWrap) * LateralWrap - LateralWrap * 0.5f;
		}
		if (FMath::Abs(Across) > Half.Y + ReachLateral) continue;
		if (FMath::Abs(P->GetPickupHeight() - Height) > Half.Z + ReachHeight) continue;
		Out.Add(P);
	}
}
