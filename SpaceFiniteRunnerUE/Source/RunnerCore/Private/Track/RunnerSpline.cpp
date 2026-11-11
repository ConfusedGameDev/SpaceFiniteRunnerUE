#include "Track/RunnerSpline.h"

// ------------------------------------------------------------------ maths

namespace RunnerMath
{
	FVector3f Normalize(const FVector3f& V)
	{
		return V * (1.f / FMath::Sqrt(V.Dot(V)));
	}

	FVector3f NormalizeSafe(const FVector3f& V, const FVector3f& Default)
	{
		const float LenSq = V.Dot(V);
		return LenSq > FLT_MIN ? V * (1.f / FMath::Sqrt(LenSq)) : Default;
	}

	FVector3f Normalized(const FVector3f& V)
	{
		const float Mag = V.Length();
		return Mag > 1e-5f ? V / Mag : FVector3f::ZeroVector;
	}

	FVector3f ProjectOnPlane(const FVector3f& V, const FVector3f& Normal)
	{
		const float SqrMag = Normal.Dot(Normal);
		if (SqrMag < UE_SMALL_NUMBER)
		{
			return V;
		}
		return V - Normal * (V.Dot(Normal) / SqrMag);
	}

	FVector3f Slerp(const FVector3f& A, const FVector3f& B, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float LenA = A.Length();
		const float LenB = B.Length();
		if (LenA < 1e-6f || LenB < 1e-6f)
		{
			return A + (B - A) * T;
		}
		const FVector3f DirA = A / LenA;
		const FVector3f DirB = B / LenB;
		const float Dot = FMath::Clamp(DirA.Dot(DirB), -1.f, 1.f);
		const float Length = LenA + (LenB - LenA) * T;
		if (Dot > 1.f - 1e-6f)
		{
			return RunnerMath::Normalized(DirA + (DirB - DirA) * T) * Length;
		}
		FVector3f Axis = DirA.Cross(DirB);
		if (Axis.SizeSquared() < 1e-12f)
		{
			// Opposite: any axis perpendicular to A.
			Axis = DirA.Cross(FMath::Abs(DirA.X) < 0.9f ? FVector3f(1.f, 0.f, 0.f) : FVector3f(0.f, 1.f, 0.f));
		}
		Axis.Normalize();
		const float Angle = FMath::Acos(Dot);
		return FQuat4f(Axis, Angle * T).RotateVector(DirA) * Length;
	}

	bool Approximately(float A, float B)
	{
		return FMath::Abs(B - A) < FMath::Max(1e-6f * FMath::Max(FMath::Abs(A), FMath::Abs(B)), 1.401298e-45f * 8.f);
	}

	FQuat4f LookRotation(const FVector3f& InForward, const FVector3f& InUp)
	{
		const float ForwardLenSq = InForward.Dot(InForward);
		const float UpLenSq = InUp.Dot(InUp);
		const FVector3f Fwd = InForward * (1.f / FMath::Sqrt(ForwardLenSq));
		const FVector3f UpDir = InUp * (1.f / FMath::Sqrt(UpLenSq));
		FVector3f Side = UpDir.Cross(Fwd);
		const float SideLenSq = Side.Dot(Side);
		Side *= 1.f / FMath::Sqrt(SideLenSq);
		const float Mn = FMath::Min3(ForwardLenSq, UpLenSq, SideLenSq);
		const float Mx = FMath::Max3(ForwardLenSq, UpLenSq, SideLenSq);
		const bool bAccept = Mn > 1e-35f && Mx < 1e35f && FMath::IsFinite(ForwardLenSq) && FMath::IsFinite(UpLenSq) && FMath::IsFinite(SideLenSq);
		if (!bAccept)
		{
			return FQuat4f::Identity;
		}
		// Unity's basis (x = side, y = forward × side, z = forward) in Unreal axes: X forward, Y side (right), Z up.
		const FVector3f Top = Fwd.Cross(Side);
		const FMatrix44f M(
			FPlane4f(Fwd.X, Fwd.Y, Fwd.Z, 0.f),
			FPlane4f(Side.X, Side.Y, Side.Z, 0.f),
			FPlane4f(Top.X, Top.Y, Top.Z, 0.f),
			FPlane4f(0.f, 0.f, 0.f, 1.f));
		FQuat4f Q(M);
		Q.Normalize();
		return Q;
	}

	float SignedAngle(const FVector3f& From, const FVector3f& To, const FVector3f& Axis)
	{
		const float Denominator = FMath::Sqrt(From.SizeSquared() * To.SizeSquared());
		if (Denominator < 1e-15f)
		{
			return 0.f;
		}
		const float Dot = FMath::Clamp(From.Dot(To) / Denominator, -1.f, 1.f);
		const float Unsigned = FMath::RadiansToDegrees(FMath::Acos(Dot));
		const FVector3f Cross = From.Cross(To);
		const float Sign = FMath::Sign(Axis.Dot(Cross));
		return Unsigned * (Sign == 0.f ? 1.f : Sign);
	}
}

using namespace RunnerMath;

namespace
{
	FVector3f AutoSmoothTangent(const FVector3f& Previous, const FVector3f& Current, const FVector3f& Next, float Tension)
	{
		const float D1 = (Current - Previous).Length();
		const float D2 = (Next - Current).Length();
		if (D1 == 0.f)
		{
			return (Next - Current) * 0.1f;
		}
		if (D2 == 0.f)
		{
			return (Current - Previous) * 0.1f;
		}
		const float A = Tension;
		const float TwoA = 2.f * Tension;
		const float D1PowA = FMath::Pow(D1, A);
		const float D1Pow2A = FMath::Pow(D1, TwoA);
		const float D2PowA = FMath::Pow(D2, A);
		const float D2Pow2A = FMath::Pow(D2, TwoA);
		return (Next * D1Pow2A - Previous * D2Pow2A + Current * (D2Pow2A - D1Pow2A)) / (3.f * D1PowA * (D1PowA + D2PowA));
	}

	FQuat4f KnotRotation(FVector3f Tangent, const FVector3f& Normal)
	{
		if (Tangent.Dot(Tangent) == 0.f)
		{
			Tangent = FQuat4f::FindBetweenVectors(UnitUp, Normal).RotateVector(UnitForward);
		}
		const FVector3f TangentDir = NormalizeSafe(Tangent);
		const FVector3f UpDir = Approximately(FMath::Abs(TangentDir.Dot(NormalizeSafe(Normal))), 1.f)
			? TangentDir.Cross(UnitRight)
			: Normalized(ProjectOnPlane(Normal, Tangent));
		return LookRotation(TangentDir, UpDir);
	}

	FRunnerSplineKnot AutoSmoothKnot(const FVector3f& Position, const FVector3f& Previous, const FVector3f& Next, const FVector3f& Normal, float Tension)
	{
		const FVector3f TanIn = AutoSmoothTangent(Next, Position, Previous, Tension);
		const FVector3f TanOut = AutoSmoothTangent(Previous, Position, Next, Tension);
		float DirIn = TanIn.Length();
		float DirOut = TanOut.Length();
		FVector3f DirRot = TanOut;
		if (DirIn == 0.f)
		{
			DirIn = DirOut;
		}
		if (DirOut == 0.f)
		{
			DirOut = DirIn;
			DirRot = -TanIn;
		}
		return FRunnerSplineKnot(Position, UnitForward * -DirIn, UnitForward * DirOut, KnotRotation(DirRot, Normal));
	}

	FRunnerSplineKnot BakeTangentDirection(const FRunnerSplineKnot& Knot, bool bMirrored)
	{
		const FQuat4f Rotation = KnotRotation(Knot.Rotation.RotateVector(Knot.TangentOut), Knot.Rotation.RotateVector(UnitUp));
		if (bMirrored)
		{
			const float Lead = Knot.TangentOut.Length();
			return FRunnerSplineKnot(Knot.Position, UnitForward * -Lead, UnitForward * Lead, Rotation);
		}
		return FRunnerSplineKnot(Knot.Position, UnitForward * -Knot.TangentIn.Length(), UnitForward * Knot.TangentOut.Length(), Rotation);
	}

	struct FFrame
	{
		FVector3f Origin, Tangent, Normal, Binormal;
	};

	FFrame NextRotationMinimizingFrame(const FRunnerBezierCurve& Curve, const FFrame& Previous, float NextT)
	{
		FFrame Next;
		Next.Origin = Curve.EvaluatePosition(NextT);
		Next.Tangent = Curve.EvaluateTangent(NextT);

		const FVector3f ToCurrent = Next.Origin - Previous.Origin;
		const float C1 = ToCurrent.Dot(ToCurrent);
		const FVector3f RiL = Previous.Binormal - ToCurrent * 2.f / C1 * ToCurrent.Dot(Previous.Binormal);
		const FVector3f TiL = Previous.Tangent - ToCurrent * 2.f / C1 * ToCurrent.Dot(Previous.Tangent);

		const FVector3f V2 = Next.Tangent - TiL;
		const float C2 = V2.Dot(V2);
		Next.Binormal = Normalize(RiL - V2 * 2.f / C2 * V2.Dot(RiL));
		Next.Normal = Normalize(Next.Binormal.Cross(Next.Tangent));
		return Next;
	}

	FVector3f EvaluateUpVector(FRunnerBezierCurve Curve, float T, const FVector3f& StartUp, const FVector3f& EndUp)
	{
		const float LinearTangentLen = ((Curve.P3 - Curve.P0) / 3.f).Length();
		const FVector3f LinearTangentOut = Normalize(Curve.P3 - Curve.P0) * LinearTangentLen;
		if (Approximately((Curve.P1 - Curve.P0).Length(), 0.f))
		{
			Curve.P1 = Curve.P0 + LinearTangentOut;
		}
		if (Approximately((Curve.P2 - Curve.P3).Length(), 0.f))
		{
			Curve.P2 = Curve.P3 - LinearTangentOut;
		}

		constexpr int32 N = FRunnerSpline::NormalsPerCurve;
		FVector3f NormalBuffer[N];

		FFrame Frame;
		Frame.Origin = Curve.P0;
		Frame.Tangent = Curve.P1 - Curve.P0;
		Frame.Normal = StartUp;
		Frame.Binormal = Normalize(Frame.Tangent.Cross(Frame.Normal));
		if (FMath::IsNaN(Frame.Binormal.X))
		{
			return FVector3f::ZeroVector;
		}
		NormalBuffer[0] = Frame.Normal;

		const float StepSize = 1.f / (N - 1);
		float CurrentT = StepSize;
		float PrevT = 0.f;
		FVector3f UpVector = FVector3f::ZeroVector;
		for (int32 i = 1; i < N; ++i)
		{
			const FFrame PrevFrame = Frame;
			Frame = NextRotationMinimizingFrame(Curve, PrevFrame, CurrentT);
			NormalBuffer[i] = Frame.Normal;
			if (PrevT <= T && CurrentT >= T)
			{
				UpVector = Slerp(PrevFrame.Normal, Frame.Normal, (T - PrevT) / StepSize);
			}
			PrevT = CurrentT;
			CurrentT += StepSize;
		}

		if (PrevT <= T && CurrentT >= T)
		{
			UpVector = EndUp;
		}

		const FVector3f LastFrameNormal = NormalBuffer[N - 1];
		float AngleBetween = FMath::Acos(FMath::Clamp(LastFrameNormal.Dot(EndUp), -1.f, 1.f));
		if (AngleBetween == 0.f)
		{
			return UpVector;
		}

		const FVector3f LastTangent = Normalize(Frame.Tangent);
		const FQuat4f Positive(LastTangent, AngleBetween);
		const FQuat4f Negative(LastTangent, -AngleBetween);
		const float PositiveResult = FMath::Acos(FMath::Clamp(Positive.RotateVector(EndUp).Dot(LastFrameNormal), -1.f, 1.f));
		const float NegativeResult = FMath::Acos(FMath::Clamp(Negative.RotateVector(EndUp).Dot(LastFrameNormal), -1.f, 1.f));
		if (PositiveResult > NegativeResult)
		{
			AngleBetween *= -1.f;
		}

		CurrentT = StepSize;
		PrevT = 0.f;
		for (int32 i = 1; i < N; ++i)
		{
			const float Adjustment = AngleBetween * CurrentT;
			const FVector3f Tangent = Normalize(Curve.EvaluateTangent(CurrentT));
			NormalBuffer[i] = FQuat4f(Tangent, -Adjustment).RotateVector(NormalBuffer[i]);
			if (PrevT <= T && CurrentT >= T)
			{
				return Slerp(NormalBuffer[i - 1], NormalBuffer[i], (T - PrevT) / StepSize);
			}
			PrevT = CurrentT;
			CurrentT += StepSize;
		}
		return EndUp;
	}
}

// ------------------------------------------------------------------ knot

FRunnerSplineKnot FRunnerSplineKnot::FromLayout(const FTrackKnot& Knot)
{
	return FRunnerSplineKnot(FVector3f(Knot.Position), FVector3f(Knot.TangentIn), FVector3f(Knot.TangentOut), FQuat4f(Knot.Rotation));
}

FTrackKnot FRunnerSplineKnot::ToLayout(ETrackTangentMode Mode) const
{
	FTrackKnot Out;
	Out.Position = FVector(Position);
	Out.Rotation = FQuat(Rotation);
	Out.TangentIn = FVector(TangentIn);
	Out.TangentOut = FVector(TangentOut);
	Out.Mode = Mode;
	return Out;
}

// ------------------------------------------------------------------ curve

FVector3f FRunnerBezierCurve::EvaluatePosition(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	const float T2 = T * T;
	const float T3 = T2 * T;
	return P0 * (-1.f * T3 + 3.f * T2 - 3.f * T + 1.f)
		+ P1 * (3.f * T3 - 6.f * T2 + 3.f * T)
		+ P2 * (-3.f * T3 + 3.f * T2)
		+ P3 * T3;
}

FVector3f FRunnerBezierCurve::EvaluateTangent(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	const float T2 = T * T;
	return P0 * (-3.f * T2 + 6.f * T - 3.f)
		+ P1 * (9.f * T2 - 12.f * T + 3.f)
		+ P2 * (-9.f * T2 + 6.f * T)
		+ P3 * (3.f * T2);
}

// ------------------------------------------------------------------ spline

void FRunnerSpline::Reset()
{
	Knots.Reset();
	Modes.Reset();
	Caches.Reset();
	CumulativeEnd.Reset();
	bCumulativeValid = false;
}

void FRunnerSpline::Add(const FRunnerSplineKnot& Knot, ETrackTangentMode Mode)
{
	const int32 Index = Knots.Add(Knot);
	Modes.Add(Mode);
	Caches.AddDefaulted();
	if (Index > 0)
	{
		ApplyTangentMode(Index - 1);
	}
	ApplyTangentMode(Index);
	Invalidate(Index);
	if (Index > 0)
	{
		Invalidate(Index - 1);
	}
}

void FRunnerSpline::Truncate(int32 Keep)
{
	Keep = FMath::Max(1, Keep);
	if (Knots.Num() <= Keep)
	{
		return;
	}
	while (Knots.Num() > Keep)
	{
		Knots.Pop(EAllowShrinking::No);
		Modes.Pop(EAllowShrinking::No);
		Caches.Pop(EAllowShrinking::No);
		ApplyTangentMode(Knots.Num() - 1);
	}
	Invalidate(Knots.Num() - 1);
}

void FRunnerSpline::ApplyTangentMode(int32 Index)
{
	FRunnerSplineKnot& Knot = Knots[Index];
	switch (Modes[Index])
	{
	case ETrackTangentMode::Continuous:
		Knot = BakeTangentDirection(Knot, false);
		break;
	case ETrackTangentMode::Mirrored:
		Knot = BakeTangentDirection(Knot, true);
		break;
	case ETrackTangentMode::Linear:
		Knot.TangentIn = FVector3f::ZeroVector;
		Knot.TangentOut = FVector3f::ZeroVector;
		break;
	case ETrackTangentMode::AutoSmooth:
	{
		const FVector3f Previous = Knots[FMath::Max(Index - 1, 0)].Position;
		const FVector3f Next = Knots[FMath::Min(Index + 1, Knots.Num() - 1)].Position;
		Knot = AutoSmoothKnot(Knot.Position, Previous, Next, Knot.Rotation.RotateVector(UnitUp), CatmullRomTension);
		break;
	}
	default:
		break;
	}
}

void FRunnerSpline::Invalidate(int32 KnotIndex)
{
	// A knot shapes the curve that ends at it and the one that starts at it.
	for (int32 Curve = KnotIndex - 1; Curve <= KnotIndex; ++Curve)
	{
		if (Caches.IsValidIndex(Curve))
		{
			Caches[Curve].bLengthValid = false;
			Caches[Curve].bUpsValid = false;
		}
	}
	bCumulativeValid = false;
}

FRunnerBezierCurve FRunnerSpline::GetCurve(int32 Index) const
{
	const FRunnerSplineKnot& A = Knots[Index];
	const FRunnerSplineKnot& B = Knots[FMath::Min(Index + 1, Knots.Num() - 1)];
	FRunnerBezierCurve Curve;
	Curve.P0 = A.Position;
	Curve.P1 = A.Position + A.Rotation.RotateVector(A.TangentOut);
	Curve.P2 = B.Position + B.Rotation.RotateVector(B.TangentIn);
	Curve.P3 = B.Position;
	return Curve;
}

const FRunnerSpline::FCurveCache& FRunnerSpline::CurveLengths(int32 Index) const
{
	FCurveCache& Cache = Caches[Index];
	if (!Cache.bLengthValid)
	{
		const FRunnerBezierCurve Curve = GetCurve(Index);
		float Magnitude = 0.f;
		FVector3f Prev = Curve.EvaluatePosition(0.f);
		Cache.Lut[0] = { 0.f, 0.f };
		for (int32 i = 1; i < LutResolution; ++i)
		{
			const float T = i / (LutResolution - 1.f);
			const FVector3f Point = Curve.EvaluatePosition(T);
			Magnitude += (Point - Prev).Length();
			Cache.Lut[i] = { Magnitude, T };
			Prev = Point;
		}
		Cache.bLengthValid = true;
	}
	return Cache;
}

float FRunnerSpline::GetCurveLength(int32 Index) const
{
	if (!Caches.IsValidIndex(Index) || Index >= NumCurves())
	{
		return 0.f;
	}
	return CurveLengths(Index).Lut[LutResolution - 1].Distance;
}

void FRunnerSpline::EnsureCumulative() const
{
	if (bCumulativeValid)
	{
		return;
	}
	const int32 Curves = NumCurves();
	CumulativeEnd.SetNumUninitialized(Curves);
	float Start = 0.f;
	for (int32 i = 0; i < Curves; ++i)
	{
		Start += GetCurveLength(i);
		CumulativeEnd[i] = Start;
	}
	bCumulativeValid = true;
}

float FRunnerSpline::GetLength() const
{
	EnsureCumulative();
	return CumulativeEnd.Num() > 0 ? CumulativeEnd.Last() : 0.f;
}

int32 FRunnerSpline::DistanceToCurveT(float Distance, float& OutCurveT) const
{
	const int32 Curves = NumCurves();
	if (Curves <= 0)
	{
		OutCurveT = 0.f;
		return 0;
	}
	EnsureCumulative();
	const float TLength = FMath::Clamp(Distance, 0.f, CumulativeEnd.Last());

	// First curve whose end reaches the distance (Unity's linear scan, as a binary search).
	int32 Lo = 0;
	int32 Hi = Curves - 1;
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (TLength <= CumulativeEnd[Mid])
		{
			Hi = Mid;
		}
		else
		{
			Lo = Mid + 1;
		}
	}
	if (TLength > CumulativeEnd[Lo])
	{
		OutCurveT = 1.f;
		return Curves - 1;
	}

	const float Start = Lo > 0 ? CumulativeEnd[Lo - 1] : 0.f;
	const float CurveDistance = TLength - Start;
	const FCurveCache& Cache = CurveLengths(Lo);
	const float CurveLength = Cache.Lut[LutResolution - 1].Distance;
	if (CurveDistance <= 0.f)
	{
		OutCurveT = 0.f;
	}
	else if (CurveDistance >= CurveLength)
	{
		OutCurveT = 1.f;
	}
	else
	{
		OutCurveT = 1.f;
		FDistanceToT Prev = Cache.Lut[0];
		for (int32 i = 1; i < LutResolution; ++i)
		{
			const FDistanceToT& Current = Cache.Lut[i];
			if (CurveDistance < Current.Distance)
			{
				OutCurveT = FMath::Lerp(Prev.T, Current.T, (CurveDistance - Prev.Distance) / (Current.Distance - Prev.Distance));
				break;
			}
			Prev = Current;
		}
	}
	return Lo;
}

FVector3f FRunnerSpline::GetCurveUpVector(int32 Index, float T) const
{
	FCurveCache& Cache = Caches[Index];
	if (!Cache.bUpsValid)
	{
		const FRunnerBezierCurve Curve = GetCurve(Index);
		const FVector3f StartUp = Knots[Index].Rotation.RotateVector(UnitUp);
		const FVector3f EndUp = Knots[FMath::Min(Index + 1, Knots.Num() - 1)].Rotation.RotateVector(UnitUp);
		Cache.Ups[0] = StartUp;
		Cache.Ups[LutResolution - 1] = EndUp;
		for (int32 i = 1; i < LutResolution - 1; ++i)
		{
			Cache.Ups[i] = EvaluateUpVector(Curve, i / static_cast<float>(LutResolution - 1), StartUp, EndUp);
		}
		Cache.bUpsValid = true;
	}

	const float Offset = 1.f / static_cast<float>(LutResolution - 1);
	float CurveT = 0.f;
	for (int32 i = 0; i < LutResolution - 1; ++i)
	{
		if (T <= CurveT + Offset)
		{
			const float Alpha = FMath::Clamp((T - CurveT) / Offset, 0.f, 1.f);
			return Cache.Ups[i] + (Cache.Ups[i + 1] - Cache.Ups[i]) * Alpha;
		}
		CurveT += Offset;
	}
	return Cache.Ups[LutResolution - 1];
}

void FRunnerSpline::Evaluate(float Distance, FVector3f& OutPosition, FVector3f& OutTangent, FVector3f& OutUp) const
{
	if (Knots.Num() == 0)
	{
		OutPosition = FVector3f::ZeroVector;
		OutTangent = UnitForward;
		OutUp = UnitUp;
		return;
	}
	if (Knots.Num() == 1)
	{
		OutPosition = Knots[0].Position;
		OutTangent = Knots[0].Rotation.RotateVector(UnitForward);
		OutUp = Knots[0].Rotation.RotateVector(UnitUp);
		return;
	}
	float CurveT = 0.f;
	const int32 Index = DistanceToCurveT(Distance, CurveT);
	const FRunnerBezierCurve Curve = GetCurve(Index);
	OutPosition = Curve.EvaluatePosition(CurveT);
	OutTangent = Curve.EvaluateTangent(CurveT);
	OutUp = GetCurveUpVector(Index, CurveT);
}
