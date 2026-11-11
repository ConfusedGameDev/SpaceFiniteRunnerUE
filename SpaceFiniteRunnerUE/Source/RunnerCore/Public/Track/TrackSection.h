#pragma once

#include "CoreMinimal.h"

class UTrackModel;

/**
 * A stretch of TRACK DISTANCE with its own pose function instead of the spline's. It covers
 * SplineExtent metres of spline and Length metres of track: an overlay (a tube) covers as much
 * spline as track and only reshapes the pose, an insert (a loop) is longer than the spline under it
 * and every distance past it shifts by the difference (InsertedLength). Sections are registered
 * the moment the generator decides a feature's spot, before anything is placed beyond it.
 * Metres, Unreal axes, in the track's own space.
 */
class RUNNERCORE_API FTrackSection
{
public:
	explicit FTrackSection(float InStartDistance) : StartDistance(InStartDistance) {}
	virtual ~FTrackSection() = default;

	float GetStartDistance() const { return StartDistance; }
	virtual float GetLength() const = 0;
	float GetSplineExtent() const { return SplineExtent; }
	float GetInsertedLength() const { return FMath::Max(0.f, GetLength() - SplineExtent); }
	bool InsertsDistance() const { return GetInsertedLength() > 0.01f; }
	float GetEndDistance() const { return StartDistance + GetLength(); }
	bool Contains(float Distance) const { return Distance >= StartDistance && Distance < GetEndDistance(); }

	/** Pose at Local metres into the section, Lateral across. SplineStart is the spline distance under the start (for overlays). */
	virtual void GetPose(const UTrackModel& Track, float SplineStart, float Local, float Lateral, FVector& OutPosition, FQuat& OutRotation) const = 0;

	/** The steering lane at a local distance; the plain road's ±half width unless the section reshapes it. */
	virtual void GetLateralBand(float Local, float TrackHalfWidth, float& OutMin, float& OutMax) const
	{
		OutMin = -TrackHalfWidth;
		OutMax = TrackHalfWidth;
	}

	/** True when the plain road's banked shoulders carry on through the section. */
	virtual bool HasShoulders() const { return true; }

	virtual class FLoopSection* AsLoop() { return nullptr; }
	virtual class FTubeSection* AsTube() { return nullptr; }
	virtual const class FLoopSection* AsLoop() const { return nullptr; }
	virtual const class FTubeSection* AsTube() const { return nullptr; }

protected:
	float StartDistance = 0.f;
	float SplineExtent = 0.f;
};

/**
 * A vertical loop standing on the track in the plane of the entry pose: Turns circles of Radius
 * that may drift sideways, carry forward and yaw the exit, all eased by a smoothstep of the ride
 * fraction so both mouths are tangent to the road. Parameterised by arc length through a table
 * built once. The inserted distance is the ride minus the bridge the generator lays between the
 * entry and exit knots (SetSplineExtent).
 */
class RUNNERCORE_API FLoopSection : public FTrackSection
{
public:
	static constexpr int32 Samples = 256;

	FLoopSection(float InStartDistance, float InRadius, const FVector& InOrigin, const FQuat& EntryRotation,
		float InLateralDrift = 0.f, float InForwardCarry = 0.f, float InExitYawDegrees = 0.f, int32 InTurns = 1);

	virtual float GetLength() const override { return Length; }
	virtual bool HasShoulders() const override { return false; }
	virtual void GetPose(const UTrackModel& Track, float SplineStart, float Local, float Lateral, FVector& OutPosition, FQuat& OutRotation) const override;
	virtual FLoopSection* AsLoop() override { return this; }
	virtual const FLoopSection* AsLoop() const override { return this; }

	void SetSplineExtent(float Metres) { SplineExtent = FMath::Max(0.f, Metres); }

	float GetRadius() const { return Radius; }
	int32 GetTurns() const { return Turns; }
	float GetLateralDrift() const { return LateralDrift; }
	float GetForwardCarry() const { return ForwardCarry; }
	float GetExitYawDegrees() const { return ExitYawDegrees; }

	/** Highest point of the first turn, on the centre line. */
	FVector GetTop() const { return PointAt(0.5f / Turns, 0.f); }
	/** Local distance of the top of the first turn, where a failed loop lets go. */
	float GetFirstTopLocal() const { return Length / (2.f * Turns); }
	/** Heading the track continues with after the exit. */
	FVector GetExitForward() const { return Fwd(1.f); }
	/** Ride fraction (0 mouth, 1 exit) for a local distance. */
	float UAt(float Local) const;
	/** Pose on the flight line at the exit. */
	void GetExitPose(float Lateral, FVector& OutPosition, FQuat& OutRotation) const;

private:
	static float Ease(float U) { return U * U * (3.f - 2.f * U); }
	FQuat Yaw(float U) const;
	FVector Fwd(float U) const;
	FVector RightAt(float U) const;
	FVector Centre(float U) const;
	FVector PointAt(float U, float Lateral) const;

	float Radius;
	int32 Turns;
	float LateralDrift;
	float ForwardCarry;
	float ExitYawDegrees;
	FVector Origin;
	FVector Forward;
	FVector Up;
	FVector Right;
	TArray<float> Arc;
	float Length = 1.f;
};

/**
 * A cylinder section: the road curls into a pipe of Radius whose top is the flight line. Lateral
 * becomes arc (angle = lateral / radius); the lane is ±BandRadians round CentreRadians. A full tube
 * (band ≥ 180°) is unbounded once curled. CurlLength eases flat ↔ pipe at each end, ReturnLength
 * before the curl-out hands the ship back to the top. Overlay: inserts no distance.
 */
class RUNNERCORE_API FTubeSection : public FTrackSection
{
public:
	FTubeSection(float InStartDistance, float InLength, float InRadius, float BandDegrees, float CentreDegrees,
		float InCurlLength, float InReturnLength, float InSteeringFactor = 1.f);

	virtual float GetLength() const override { return Length; }
	virtual bool HasShoulders() const override { return false; }
	virtual void GetPose(const UTrackModel& Track, float SplineStart, float Local, float Lateral, FVector& OutPosition, FQuat& OutRotation) const override;
	virtual void GetLateralBand(float Local, float TrackHalfWidth, float& OutMin, float& OutMax) const override;
	virtual FTubeSection* AsTube() override { return this; }
	virtual const FTubeSection* AsTube() const override { return this; }

	float GetRadius() const { return Radius; }
	float GetBandRadians() const { return BandRadians; }
	float GetCentreRadians() const { return CentreRadians; }
	float GetCurlLength() const { return CurlLength; }
	float GetReturnLength() const { return ReturnLength; }
	float GetSteeringFactor() const { return SteeringFactor; }
	bool IsUnbounded() const { return bUnbounded; }
	float GetCircumference() const { return 2.f * UE_PI * Radius; }

	bool IsUnboundedAt(float Local) const { return bUnbounded && Curl(Local) >= 0.999f; }
	/** 0 while the player steers freely, easing to 1 at the start of the curl-out. */
	float ReturnProgress(float Local) const;
	/** 0 on flat road, 1 fully curled. */
	float Curl(float Local) const;

private:
	float Length;
	float Radius;
	float BandRadians;
	float CentreRadians;
	float CurlLength;
	float ReturnLength;
	float SteeringFactor;
	bool bUnbounded;
};
