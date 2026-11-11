#include "Track/Features/TrackFeatureDefinitions.h"

#include "Track/RunnerRandom.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"

TSharedPtr<FTrackSection> UTrackFeatureDefinition::CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const
{
	return nullptr;
}

TSharedPtr<FTrackSection> ULoopDefinition::CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const
{
	FVector Origin;
	FQuat Rotation;
	Track.GetPoseAtDistance(StartDistance, 0.f, Origin, Rotation);
	// Fixed draw order, so a seed (or a saved state) reproduces the loop: drift, its side, carry, yaw, its side, turns.
	float Drift = Rng.NextFloat(GetDriftMin(), GetDriftMax());
	if (Rng.NextFloat(0.f, 1.f) < 0.5f) Drift = -Drift;
	const float Carry = Rng.NextFloat(GetCarryMin(), GetCarryMax());
	float Yaw = Rng.NextFloat(GetYawMin(), GetYawMax());
	if (Rng.NextFloat(0.f, 1.f) < 0.5f) Yaw = -Yaw;
	const int32 Turns = Rng.NextInt(GetTurnsMin(), GetTurnsMax() + 1);
	return MakeShared<FLoopSection>(StartDistance, Radius, Origin, Rotation, Drift, Carry, Yaw, Turns);
}

TSharedPtr<FTrackSection> UTubeDefinition::CreateSection(const UTrackModel& Track, float StartDistance, FRunnerRandom& Rng) const
{
	const float SectionLength = Rng.NextFloat(LengthRange.Min, FMath::Max(LengthRange.Min, LengthRange.Max));
	return MakeShared<FTubeSection>(StartDistance, SectionLength, Radius, BandDegrees, CentreDegrees, CurlLength, ReturnLength, SteeringFactor);
}
