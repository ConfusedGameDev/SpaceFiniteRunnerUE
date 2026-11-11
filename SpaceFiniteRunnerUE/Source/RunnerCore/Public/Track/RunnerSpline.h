#pragma once

#include "CoreMinimal.h"
#include "Track/TrackLayout.h"

/** One Bezier knot as Unity.Splines stores it: tangents in the knot's local frame. Unreal axes, metres. */
struct FRunnerSplineKnot
{
	FVector3f Position = FVector3f::ZeroVector;
	FVector3f TangentIn = FVector3f::ZeroVector;
	FVector3f TangentOut = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity;

	FRunnerSplineKnot() = default;
	FRunnerSplineKnot(const FVector3f& InPosition, const FVector3f& InTangentIn, const FVector3f& InTangentOut, const FQuat4f& InRotation)
		: Position(InPosition), TangentIn(InTangentIn), TangentOut(InTangentOut), Rotation(InRotation) {}

	static FRunnerSplineKnot FromLayout(const FTrackKnot& Knot);
	FTrackKnot ToLayout(ETrackTangentMode Mode) const;
};

/** A cubic Bezier segment between two knots (P0, P1 = out handle, P2 = in handle, P3). */
struct FRunnerBezierCurve
{
	FVector3f P0, P1, P2, P3;

	FVector3f EvaluatePosition(float T) const;
	FVector3f EvaluateTangent(float T) const;
};

/**
 * The parts of Unity.Splines 2.x the track uses, ported line for line so a saved track's knots,
 * replayed in order, give the same road:
 * - AutoSmooth knots (Catmull-Rom, tension ½) re-derive their tangents and re-lean their rotation
 *   off their neighbours every time a knot lands next to them; Continuous knots keep an explicit
 *   tangent and bake its direction into the rotation.
 * - Every curve caches a 30-entry distance → t table (chord sums) and a 30-entry up-vector table
 *   built from 16 rotation-minimising frames with the end-up mismatch spread along the curve.
 * - A distance along the spline finds its curve (binary search over the cumulative lengths), its t
 *   through the table, and its up by a linear blend of the table.
 * Floats throughout, like Unity. Not thread safe: the caches fill lazily.
 */
class RUNNERCORE_API FRunnerSpline
{
public:
	static constexpr int32 LutResolution = 30;
	static constexpr int32 NormalsPerCurve = 16;
	static constexpr float CatmullRomTension = 0.5f;

	void Reset();

	/** Appends a knot with its tangent mode (Unity's Spline.Add(knot, mode)): the previous knot's tangents are re-applied. */
	void Add(const FRunnerSplineKnot& Knot, ETrackTangentMode Mode);

	/** Drops knots from the end until Keep remain (at least 1). The new last knot is re-applied like Unity's RemoveAt. */
	void Truncate(int32 Keep);

	int32 Num() const { return Knots.Num(); }
	int32 NumCurves() const { return FMath::Max(0, Knots.Num() - 1); }
	const FRunnerSplineKnot& GetKnot(int32 Index) const { return Knots[Index]; }
	ETrackTangentMode GetMode(int32 Index) const { return Modes[Index]; }

	FRunnerBezierCurve GetCurve(int32 Index) const;
	float GetCurveLength(int32 Index) const;
	float GetLength() const;

	/** Position, tangent (not normalised) and up (not normalised) at a distance along the spline, clamped to it. */
	void Evaluate(float Distance, FVector3f& OutPosition, FVector3f& OutTangent, FVector3f& OutUp) const;

	/** Curve index and its t for a distance along the spline. */
	int32 DistanceToCurveT(float Distance, float& OutCurveT) const;

private:
	struct FDistanceToT
	{
		float Distance = -1.f;
		float T = 0.f;
	};

	struct FCurveCache
	{
		bool bLengthValid = false;
		bool bUpsValid = false;
		FDistanceToT Lut[LutResolution];
		FVector3f Ups[LutResolution];
	};

	void ApplyTangentMode(int32 Index);
	void Invalidate(int32 KnotIndex);
	const FCurveCache& CurveLengths(int32 Index) const;
	FVector3f GetCurveUpVector(int32 Index, float T) const;
	void EnsureCumulative() const;

	TArray<FRunnerSplineKnot> Knots;
	TArray<ETrackTangentMode> Modes;
	mutable TArray<FCurveCache> Caches; // one per knot (curve i starts at knot i)
	mutable TArray<float> CumulativeEnd; // spline distance at the end of each curve
	mutable bool bCumulativeValid = false;
};

/** Unity's maths helpers the spline port (and the track model) rely on, in Unreal axes. */
namespace RunnerMath
{
	inline const FVector3f UnitUp(0.f, 0.f, 1.f);
	inline const FVector3f UnitForward(1.f, 0.f, 0.f);
	inline const FVector3f UnitRight(0.f, 1.f, 0.f);

	/** math.normalize: NaN for a zero vector, like Unity. */
	RUNNERCORE_API FVector3f Normalize(const FVector3f& V);
	/** math.normalizesafe. */
	RUNNERCORE_API FVector3f NormalizeSafe(const FVector3f& V, const FVector3f& Default = FVector3f::ZeroVector);
	/** Vector3.normalized (zero below 1e-5). */
	RUNNERCORE_API FVector3f Normalized(const FVector3f& V);
	/** Vector3.ProjectOnPlane. */
	RUNNERCORE_API FVector3f ProjectOnPlane(const FVector3f& V, const FVector3f& Normal);
	/** Vector3.Slerp: direction spherically, length linearly, t clamped. */
	RUNNERCORE_API FVector3f Slerp(const FVector3f& A, const FVector3f& B, float T);
	/** Mathf.Approximately. */
	RUNNERCORE_API bool Approximately(float A, float B);
	/** quaternion.LookRotationSafe / Quaternion.LookRotation: local X along Forward, local Z toward Up. Identity when degenerate. */
	RUNNERCORE_API FQuat4f LookRotation(const FVector3f& Forward, const FVector3f& Up);
	/** Vector3.SignedAngle, degrees. */
	RUNNERCORE_API float SignedAngle(const FVector3f& From, const FVector3f& To, const FVector3f& Axis);
}
