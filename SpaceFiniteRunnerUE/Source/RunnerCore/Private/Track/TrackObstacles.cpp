#include "Track/TrackObstacles.h"

#include "PickupContracts.h"
#include "Simulation/PickupRegistry.h"
#include "Track/Features/JumpRamp.h"
#include "Track/Features/LaserGate.h"

void FTrackObstacles::Collect(TConstArrayView<TObjectPtr<AJumpRamp>> Ramps, const UPickupRegistry* Registry, float From, float To, float Pad,
	TArray<FTrackObstacle>& Out)
{
	for (const TObjectPtr<AJumpRamp>& Ramp : Ramps)
	{
		if (!Ramp || Ramp->IsEndRamp()) continue;
		if (Ramp->GetStartDistance() > To || Ramp->GetEndDistance() < From) continue;
		Out.Add({ Ramp->GetStartDistance(), Ramp->GetEndDistance(),
			Ramp->GetLateral() - Ramp->GetHalfWidth() - Pad, Ramp->GetLateral() + Ramp->GetHalfWidth() + Pad });
	}

	if (!Registry) return;
	TArray<ITrackPickup*> Pickups;
	Registry->GetAll(Pickups);
	for (ITrackPickup* Pickup : Pickups)
	{
		const ALaserGate* Gate = Cast<ALaserGate>(Cast<UObject>(Pickup));
		if (!Gate || !Gate->IsPickupAvailable()) continue;
		const float D = Gate->GetPickupDistance();
		const float Depth = Gate->GetHalfDepth();
		if (D - Depth > To || D + Depth < From) continue;
		// A vertical beam stands on the road with no emitter beside it.
		const float Across = Gate->GetPickupHalfExtents().Y + (Gate->GetVariant() == ELaserGateVariant::Vertical ? 0.f : Gate->GetEmitterReach()) + Pad;
		Out.Add({ D - Depth, D + Depth, Gate->GetPickupLateral() - Across, Gate->GetPickupLateral() + Across });
	}
}

void FTrackObstacles::FreeIntervals(float Min, float Max, TConstArrayView<FTrackObstacle> Blocked, TArray<FVector2f>& Free)
{
	Free.Reset();
	if (Max < Min) return;
	Free.Add(FVector2f(Min, Max));
	for (const FTrackObstacle& O : Blocked)
	{
		for (int32 i = Free.Num() - 1; i >= 0; --i)
		{
			const FVector2f F = Free[i];
			if (O.Max <= F.X || O.Min >= F.Y) continue;
			Free.RemoveAt(i);
			if (O.Max < F.Y) Free.Insert(FVector2f(O.Max, F.Y), i);
			if (O.Min > F.X) Free.Insert(FVector2f(F.X, O.Min), i);
		}
	}
}

bool FTrackObstacles::Nearest(TConstArrayView<FVector2f> Free, float Lateral, float& OutNearest)
{
	OutNearest = Lateral;
	float Best = TNumericLimits<float>::Max();
	for (const FVector2f& F : Free)
	{
		const float X = FMath::Clamp(Lateral, F.X, F.Y);
		const float Gap = FMath::Abs(X - Lateral);
		if (Gap < Best)
		{
			Best = Gap;
			OutNearest = X;
		}
	}
	return Best < TNumericLimits<float>::Max();
}
