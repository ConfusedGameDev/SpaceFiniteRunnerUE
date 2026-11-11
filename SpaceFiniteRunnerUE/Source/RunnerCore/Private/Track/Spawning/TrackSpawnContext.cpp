#include "Track/Spawning/TrackSpawnContext.h"

#include "TrackContracts.h"
#include "Track/Spawning/PadDefinition.h"
#include "Track/TrackModel.h"

void FTrackSpawnContext::Reset()
{
	Claims.Reset();
	PickupDistances.Reset();
	FeatureKeepOuts.Reset();
	Placements.Reset();
	Spawned.Reset();
}

float FTrackSpawnContext::GetAirLaneHeight() const
{
	return Rules != nullptr ? Rules->GetAirLaneHeight() : DefaultAirLaneHeight;
}

float FTrackSpawnContext::GetBaseBoost() const
{
	return Rules != nullptr ? Rules->GetPowerUpSpeedBoost() : DefaultPowerUpSpeedBoost;
}

float FTrackSpawnContext::ClaimEnd(float Distance) const
{
	for (const FFloatInterval& C : Claims)
	{
		if (Distance >= C.Min - PadSize.Z && Distance < C.Max + PadSize.Z)
		{
			return C.Max + PadSize.Z;
		}
	}
	return -1.f;
}

bool FTrackSpawnContext::NearPickup(float Distance) const
{
	for (const float D : PickupDistances)
	{
		if (FMath::Abs(D - Distance) < PadSize.Z)
		{
			return true;
		}
	}
	return false;
}

float FTrackSpawnContext::KeepOutUntil(float Distance, float Reach, bool bAllowFlatSweeps, bool bAllowOpenEdges) const
{
	const float Start = Distance - Reach;
	const float End = Distance + Reach;

	for (const FFloatInterval& KeepOut : FeatureKeepOuts)
	{
		if (End > KeepOut.Min && Start < KeepOut.Max)
		{
			return FMath::IsFinite(KeepOut.Max) ? KeepOut.Max + Reach : KeepOut.Max;
		}
	}

	if (!bAllowFlatSweeps)
	{
		if (const FTrackSpan* Sweep = Track->FlatSweepWithin(Start, End - Start))
		{
			return Sweep->End + Reach;
		}
	}

	// A loop or a tube is a keep-out already (belt and braces); an open edge is not.
	for (float D = Start; D <= End; D += 25.f)
	{
		if (Track->SectionAt(D) != nullptr || (!bAllowOpenEdges && (Track->IsEdgeOpen(D, -1) || Track->IsEdgeOpen(D, 1))))
		{
			return D + Reach + 25.f;
		}
	}
	return -1.f;
}

float FTrackSpawnContext::RandomLateral(FRunnerRandom& Rng, float Distance, float Margin) const
{
	float BandMin, BandMax;
	Track->GetLateralBand(Distance, BandMin, BandMax);
	const float Lo = BandMin + Margin;
	const float Hi = BandMax - Margin;
	return Hi > Lo ? Rng.NextFloat(Lo, Hi) : (BandMin + BandMax) * 0.5f;
}

float FTrackSpawnContext::ClampLateral(float Distance, float Lateral, float Margin) const
{
	float BandMin, BandMax;
	Track->GetLateralBand(Distance, BandMin, BandMax);
	const float Lo = BandMin + Margin;
	const float Hi = BandMax - Margin;
	return Hi > Lo ? FMath::Clamp(Lateral, Lo, Hi) : (BandMin + BandMax) * 0.5f;
}

float FTrackSpawnContext::PadMargin(const UPadDefinition* Definition, float Sway) const
{
	const float Width = PadSize.X * (Definition != nullptr ? Definition->SizeMultiplier : 1.f);
	return Width * 0.5f + 2.f + Sway;
}
