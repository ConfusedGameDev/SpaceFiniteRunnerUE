#include "Track/TrackSection.h"

#include "Track/RunnerSpline.h"
#include "Track/TrackModel.h"

namespace
{
	FQuat LookRotationD(const FVector& Forward, const FVector& Up)
	{
		return FQuat(RunnerMath::LookRotation(FVector3f(Forward), FVector3f(Up)));
	}

	float SmoothStep01(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return -2.f * T * T * T + 3.f * T * T;
	}
}

// ------------------------------------------------------------------ loop

FLoopSection::FLoopSection(float InStartDistance, float InRadius, const FVector& InOrigin, const FQuat& EntryRotation,
	float InLateralDrift, float InForwardCarry, float InExitYawDegrees, int32 InTurns)
	: FTrackSection(InStartDistance)
	, Radius(FMath::Max(1.f, InRadius))
	, Turns(FMath::Max(1, InTurns))
	, LateralDrift(InLateralDrift)
	, ForwardCarry(FMath::Max(0.f, InForwardCarry))
	, ExitYawDegrees(InExitYawDegrees)
	, Origin(InOrigin)
	, Forward(EntryRotation.GetForwardVector())
	, Up(EntryRotation.GetUpVector())
	, Right(EntryRotation.GetRightVector())
{
	// Arc length by chords: the ride is only ever sampled through this table.
	Arc.SetNumZeroed(Samples + 1);
	FVector Previous = PointAt(0.f, 0.f);
	for (int32 i = 1; i <= Samples; ++i)
	{
		const FVector Point = PointAt(i / static_cast<float>(Samples), 0.f);
		Arc[i] = Arc[i - 1] + static_cast<float>(FVector::Distance(Previous, Point));
		Previous = Point;
	}
	Length = FMath::Max(Arc[Samples], 1.f);
}

FQuat FLoopSection::Yaw(float U) const
{
	return FQuat(Up, FMath::DegreesToRadians(ExitYawDegrees * Ease(U)));
}

FVector FLoopSection::Fwd(float U) const { return Yaw(U).RotateVector(Forward); }
FVector FLoopSection::RightAt(float U) const { return Yaw(U).RotateVector(Right); }

FVector FLoopSection::Centre(float U) const
{
	const float E = Ease(U);
	return Origin + Fwd(U) * (ForwardCarry * E) + Up * Radius + RightAt(U) * (LateralDrift * E);
}

FVector FLoopSection::PointAt(float U, float Lateral) const
{
	const float Theta = 2.f * UE_PI * Turns * U;
	return Centre(U) + (Fwd(U) * FMath::Sin(Theta) - Up * FMath::Cos(Theta)) * Radius + RightAt(U) * Lateral;
}

float FLoopSection::UAt(float Local) const
{
	const float D = FMath::Clamp(Local, 0.f, Length);
	int32 i = FMath::Clamp(static_cast<int32>(D / Length * Samples), 0, Samples - 1);
	while (i > 0 && Arc[i] > D) --i;
	while (i < Samples - 1 && Arc[i + 1] < D) ++i;
	const float Span = Arc[i + 1] - Arc[i];
	const float T = Span > 1e-5f ? (D - Arc[i]) / Span : 0.f;
	return (i + T) / Samples;
}

void FLoopSection::GetPose(const UTrackModel& Track, float SplineStart, float Local, float Lateral, FVector& OutPosition, FQuat& OutRotation) const
{
	const float U = UAt(Local);
	OutPosition = PointAt(U, Lateral);

	constexpr float Du = 1e-3f;
	FVector Tangent = PointAt(FMath::Min(U + Du, 1.f), Lateral) - PointAt(FMath::Max(U - Du, 0.f), Lateral);
	if (Tangent.SizeSquared() < 1e-8)
	{
		Tangent = Fwd(U);
	}
	// Toward the ring's centre: up at the mouth, down at the top.
	FVector Normal = Centre(U) - PointAt(U, 0.f);
	if (Normal.SizeSquared() < 1e-8)
	{
		Normal = Up;
	}
	OutRotation = LookRotationD(Tangent.GetSafeNormal(), Normal.GetSafeNormal());
}

void FLoopSection::GetExitPose(float Lateral, FVector& OutPosition, FQuat& OutRotation) const
{
	OutPosition = PointAt(1.f, Lateral);
	OutRotation = LookRotationD(Fwd(1.f), Up);
}

// ------------------------------------------------------------------ tube

FTubeSection::FTubeSection(float InStartDistance, float InLength, float InRadius, float BandDegrees, float CentreDegrees,
	float InCurlLength, float InReturnLength, float InSteeringFactor)
	: FTrackSection(InStartDistance)
	, Length(FMath::Max(1.f, InLength))
	, Radius(FMath::Max(1.f, InRadius))
	, BandRadians(FMath::DegreesToRadians(FMath::Clamp(BandDegrees, 1.f, 180.f)))
	, CentreRadians(FMath::DegreesToRadians(CentreDegrees))
	, CurlLength(0.f)
	, ReturnLength(0.f)
	, SteeringFactor(FMath::Max(0.1f, InSteeringFactor))
	, bUnbounded(BandDegrees >= 180.f)
{
	SplineExtent = Length; // an overlay: as much spline as track
	CurlLength = FMath::Clamp(InCurlLength, 0.f, Length * 0.5f);
	ReturnLength = FMath::Clamp(InReturnLength, 0.f, FMath::Max(0.f, Length - 2.f * CurlLength));
}

float FTubeSection::ReturnProgress(float Local) const
{
	if (ReturnLength <= 0.f)
	{
		return 0.f;
	}
	const float End = Length - CurlLength;
	const float Start = End - ReturnLength;
	if (Local <= Start)
	{
		return 0.f;
	}
	return SmoothStep01((Local - Start) / ReturnLength);
}

float FTubeSection::Curl(float Local) const
{
	if (CurlLength <= 0.f)
	{
		return 1.f;
	}
	const float FromStart = FMath::Clamp(Local / CurlLength, 0.f, 1.f);
	const float FromEnd = FMath::Clamp((Length - Local) / CurlLength, 0.f, 1.f);
	return SmoothStep01(FMath::Min(FromStart, FromEnd));
}

void FTubeSection::GetPose(const UTrackModel& Track, float SplineStart, float Local, float Lateral, FVector& OutPosition, FQuat& OutRotation) const
{
	FVector Centre;
	FQuat Flat;
	Track.GetSplinePoseAtDistance(SplineStart + Local, 0.f, Centre, Flat);
	const FVector Forward = Flat.GetForwardVector();
	const FVector Up = Flat.GetUpVector();
	const FVector Right = Flat.GetRightVector();

	const float E = Curl(Local);
	const float Phi = E * Lateral / Radius;
	const FVector Radial = Up * FMath::Cos(Phi) + Right * FMath::Sin(Phi);
	const FVector TubePosition = Centre - Up * Radius + Radial * Radius;
	const FVector FlatPosition = Centre + Right * Lateral;

	OutPosition = FMath::Lerp(FlatPosition, TubePosition, static_cast<double>(E));
	const FVector UpDir = FVector(RunnerMath::Slerp(FVector3f(Up), FVector3f(Radial), E));
	OutRotation = LookRotationD(Forward, UpDir);
}

void FTubeSection::GetLateralBand(float Local, float TrackHalfWidth, float& OutMin, float& OutMax) const
{
	const float E = Curl(Local);
	const float Half = FMath::Lerp(TrackHalfWidth, BandRadians * Radius, E);
	const float CentreLateral = FMath::Lerp(0.f, CentreRadians * Radius, E);
	OutMin = CentreLateral - Half;
	OutMax = CentreLateral + Half;
}
