#include "Track/TrackModel.h"

#include "Track/TrackSection.h"
#include "Track/RunnerRandom.h"
#include "Track/TrackShape.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunnerTrack, Log, All);

using namespace RunnerMath;

void UTrackModel::Recalculate()
{
	SplineLength = Spline.GetLength();
	float Inserted = 0.f;
	for (const TSharedPtr<FTrackSection>& S : Sections)
	{
		Inserted += S->GetInsertedLength();
	}
	Length = SplineLength + Inserted;
}

void UTrackModel::ClearKnots()
{
	++Revision;
	Spline.Reset();
	Appended.Reset();
	Sections.Reset();
	FlatSweeps.Reset();
	OpenStretches.Reset();
	EndDistance = -1.f;
	EndZoneStart = -1.f;
	Recalculate();
}

void UTrackModel::TruncateKnots(int32 KeepKnots, float CutDistance)
{
	++Revision;
	Spline.Truncate(FMath::Max(1, KeepKnots));
	if (Appended.Num() > Spline.Num())
	{
		Appended.SetNum(Spline.Num());
	}
	Sections.RemoveAll([CutDistance](const TSharedPtr<FTrackSection>& S) { return S->GetStartDistance() >= CutDistance; });
	FlatSweeps.RemoveAll([CutDistance](const FTrackSpan& S) { return S.Start >= CutDistance; });
	for (FTrackSpan& S : FlatSweeps) S.End = FMath::Min(S.End, CutDistance);
	OpenStretches.RemoveAll([CutDistance](const FTrackSpan& S) { return S.Start >= CutDistance; });
	for (FTrackSpan& S : OpenStretches) S.End = FMath::Min(S.End, CutDistance);
	EndDistance = -1.f;
	EndZoneStart = -1.f;
	Recalculate();
}

void UTrackModel::AddKnot(const FTrackKnot& Knot)
{
	Spline.Add(FRunnerSplineKnot::FromLayout(Knot), Knot.Mode);
	Appended.Add(Knot);
}

void UTrackModel::AppendKnot(const FTrackKnot& Knot)
{
	AddKnot(Knot);
}

void UTrackModel::AppendKnot(const FVector& Position)
{
	FTrackKnot Knot;
	Knot.Position = Position;
	Knot.Mode = ETrackTangentMode::AutoSmooth;
	AddKnot(Knot);
}

void UTrackModel::AppendKnot(const FVector& Position, const FQuat& Rotation)
{
	FTrackKnot Knot;
	Knot.Position = Position;
	Knot.Rotation = Rotation;
	Knot.Mode = ETrackTangentMode::AutoSmooth;
	AddKnot(Knot);
}

void UTrackModel::AppendKnot(const FVector& Position, const FQuat& Rotation, const FVector& Tangent)
{
	// A knot stores its tangents in its own frame: bring the track-space tangent into it.
	const FVector3f Local = FQuat4f(Rotation).Inverse().RotateVector(FVector3f(Tangent));
	FTrackKnot Knot;
	Knot.Position = Position;
	Knot.Rotation = Rotation;
	Knot.TangentIn = FVector(-Local);
	Knot.TangentOut = FVector(Local);
	Knot.Mode = ETrackTangentMode::Continuous;
	AddKnot(Knot);
}

float UTrackModel::KnotTrackDistance(int32 Index) const
{
	if (Index <= 0)
	{
		return 0.f;
	}
	float SplineDistance = 0.f;
	for (int32 i = 0; i < Index && i < Spline.Num() - 1; ++i)
	{
		SplineDistance += Spline.GetCurveLength(i);
	}
	float Inserted = 0.f;
	for (const TSharedPtr<FTrackSection>& S : Sections)
	{
		if (SplineDistanceOf(S->GetStartDistance()) < SplineDistance - 0.01f)
		{
			Inserted += S->GetInsertedLength();
		}
	}
	return SplineDistance + Inserted;
}

void UTrackModel::AddSection(TSharedPtr<FTrackSection> Section)
{
	if (!Section.IsValid())
	{
		return;
	}
	int32 i = 0;
	while (i < Sections.Num() && Sections[i]->GetStartDistance() <= Section->GetStartDistance()) ++i;
	Sections.Insert(Section, i);
	Recalculate();
}

FTrackSection* UTrackModel::SectionAt(float Distance) const
{
	for (const TSharedPtr<FTrackSection>& S : Sections)
	{
		if (Distance < S->GetStartDistance()) return nullptr;
		if (Distance < S->GetEndDistance()) return S.Get();
	}
	return nullptr;
}

float UTrackModel::SplineDistanceOf(float Distance) const
{
	float Offset = 0.f;
	for (const TSharedPtr<FTrackSection>& S : Sections)
	{
		if (Distance < S->GetStartDistance()) break;
		if (Distance < S->GetEndDistance())
		{
			return S->InsertsDistance() ? S->GetStartDistance() - Offset : Distance - Offset;
		}
		Offset += S->GetInsertedLength();
	}
	return Distance - Offset;
}

void UTrackModel::GetLateralBand(float Distance, float& OutMin, float& OutMax) const
{
	if (const FTrackSection* S = SectionAt(Distance))
	{
		S->GetLateralBand(Distance - S->GetStartDistance(), HalfWidth, OutMin, OutMax);
	}
	else
	{
		OutMin = -HalfWidth;
		OutMax = HalfWidth;
	}
}

bool UTrackModel::HasShoulders(float Distance) const
{
	const FTrackSection* S = SectionAt(Distance);
	return (S == nullptr || S->HasShoulders()) && GetShoulderWidth() > 0.01f;
}

void UTrackModel::GetRoadBand(float Distance, float& OutMin, float& OutMax) const
{
	GetLateralBand(Distance, OutMin, OutMax);
	if (!HasShoulders(Distance))
	{
		return;
	}
	const float Shoulder = GetShoulderWidth();
	OutMin -= Shoulder;
	OutMax += Shoulder;
}

void UTrackModel::GetSplinePoseAtDistance(float SplineDistance, float Lateral, FVector& OutPosition, FQuat& OutRotation) const
{
	FVector3f Pos, Tangent, UpVec;
	Spline.Evaluate(FMath::Clamp(SplineDistance, 0.f, SplineLength), Pos, Tangent, UpVec);
	const FVector3f Fwd = NormalizeSafe(Tangent, UnitForward);
	const FVector3f UpDir = NormalizeSafe(UpVec, UnitUp);
	const FVector3f RightDir = NormalizeSafe(UpDir.Cross(Fwd), UnitRight);
	OutPosition = FVector(Pos + RightDir * Lateral);
	OutRotation = FQuat(LookRotation(Fwd, UpDir));
}

void UTrackModel::GetPoseAtDistance(float Distance, float Lateral, FVector& OutPosition, FQuat& OutRotation) const
{
	float Offset = 0.f;
	for (const TSharedPtr<FTrackSection>& S : Sections)
	{
		if (Distance < S->GetStartDistance()) break;
		if (Distance < S->GetEndDistance())
		{
			S->GetPose(*this, S->GetStartDistance() - Offset, Distance - S->GetStartDistance(), Lateral, OutPosition, OutRotation);
			return;
		}
		Offset += S->GetInsertedLength();
	}
	GetSplinePoseAtDistance(Distance - Offset, Lateral, OutPosition, OutRotation);

	// Past the end of a finite track the pose runs on along the last line, so a body that
	// overshoots by a step (and the camera behind it) never freezes on the last knot.
	if (HasEnd() && Distance > EndDistance)
	{
		OutPosition += OutRotation.GetForwardVector() * (Distance - EndDistance);
	}
}

FTransform UTrackModel::GetPoseTransform(float Distance, float Lateral) const
{
	FVector Position;
	FQuat Rotation;
	GetPoseAtDistance(Distance, Lateral, Position, Rotation);
	return FTransform(Rotation, Position);
}

FTransform UTrackModel::GetWorldPose(float Distance, float Lateral) const
{
	FVector Position;
	FQuat Rotation;
	GetPoseAtDistance(Distance, Lateral, Position, Rotation);
	return FTransform(ToWorldRotation(Rotation), ToWorldPosition(Position));
}

void UTrackModel::GetFrameAtDistance(float Distance, FVector& OutPosition, FVector& OutForward, FVector& OutUp, FVector& OutRight) const
{
	FQuat Rotation;
	GetPoseAtDistance(Distance, 0.f, OutPosition, Rotation);
	OutForward = Rotation.GetForwardVector();
	OutUp = Rotation.GetUpVector();
	OutRight = Rotation.GetRightVector();
}

float UTrackModel::GetCurvatureAtDistance(float Distance) const
{
	const float From = FMath::Max(0.f, Distance - CurvatureHalfStep);
	const float To = FMath::Min(Length, Distance + CurvatureHalfStep);
	if (To - From < 0.01f)
	{
		return 0.f;
	}
	FVector Unused;
	FQuat Before, After, Here;
	GetPoseAtDistance(From, 0.f, Unused, Before);
	GetPoseAtDistance(To, 0.f, Unused, After);
	GetPoseAtDistance(Distance, 0.f, Unused, Here);
	const FVector Turn = After.GetForwardVector() - Before.GetForwardVector();
	return static_cast<float>(FVector::DotProduct(Turn, Here.GetRightVector()) / (To - From));
}

float UTrackModel::GetBankAtDistance(float Distance) const
{
	FVector Position, Fwd, UpVec, RightVec;
	GetFrameAtDistance(Distance, Position, Fwd, UpVec, RightVec);
	const FVector LevelRight = FVector::CrossProduct(FVector::UpVector, Fwd);
	if (LevelRight.SizeSquared() < 1e-4)
	{
		return 0.f;
	}
	return SignedAngle(FVector3f(LevelRight.GetSafeNormal()), FVector3f(RightVec), FVector3f(Fwd));
}

int32 UTrackModel::AddFlatSweep(float StartDistance, float InEndDistance, int32 OuterSide)
{
	FTrackSpan Span;
	Span.Start = StartDistance;
	Span.End = FMath::Max(StartDistance, InEndDistance);
	Span.OuterSide = OuterSide < 0 ? -1 : 1;
	return FlatSweeps.Add(Span);
}

int32 UTrackModel::AddOpenStretch(float StartDistance, float InEndDistance)
{
	FTrackSpan Span;
	Span.Start = StartDistance;
	Span.End = FMath::Max(StartDistance, InEndDistance);
	Span.OuterSide = 0;
	return OpenStretches.Add(Span);
}

const FTrackSpan* UTrackModel::FlatSweepAt(float Distance) const
{
	// Newest first: the ship rides near the end of the list far more often than its start.
	for (int32 i = FlatSweeps.Num() - 1; i >= 0; --i)
	{
		const FTrackSpan& Sweep = FlatSweeps[i];
		if (Distance >= Sweep.End) return nullptr; // sorted: every earlier one ends sooner still
		if (Distance >= Sweep.Start) return SectionAt(Distance) == nullptr ? &Sweep : nullptr;
	}
	return nullptr;
}

const FTrackSpan* UTrackModel::FlatSweepWithin(float Distance, float Ahead) const
{
	for (const FTrackSpan& Sweep : FlatSweeps)
	{
		if (Sweep.End > Distance && Sweep.Start - Distance < Ahead) return &Sweep;
	}
	return nullptr;
}

bool UTrackModel::IsEdgeOpen(float Distance, int32 Side) const
{
	if (const FTrackSpan* Sweep = FlatSweepAt(Distance))
	{
		if (Sweep->OuterSide == (Side < 0 ? -1 : 1)) return true;
	}
	for (int32 i = OpenStretches.Num() - 1; i >= 0; --i)
	{
		const FTrackSpan& Stretch = OpenStretches[i];
		if (Distance >= Stretch.End) return false;
		if (Distance >= Stretch.Start) return SectionAt(Distance) == nullptr;
	}
	return false;
}

bool UTrackModel::LoadRoad(const FTrackLayout& Layout, const UTrackShape* Shape)
{
	ClearKnots();
	SetWidth(Layout.Width);
	for (const FTrackKnot& Knot : Layout.Knots)
	{
		AppendKnot(Knot);
	}
	Recalculate();

	for (const FTrackSectionRecord& Record : Layout.Sections)
	{
		const UTrackFeatureDefinition* Definition = Shape != nullptr && Shape->FeatureTable.IsValidIndex(Record.FeatureIndex)
			? Shape->FeatureTable[Record.FeatureIndex].Definition.Get()
			: nullptr;
		if (Definition == nullptr)
		{
			UE_LOG(LogRunnerTrack, Warning, TEXT("Saved section at %.0f m names feature %d, which the shape does not have: skipped."), Record.Start, Record.FeatureIndex);
			continue;
		}
		FRunnerRandom Replay = FRunnerRandom::FromState(static_cast<uint32>(Record.RngState));
		TSharedPtr<FTrackSection> Section = Definition->CreateSection(*this, Record.Start, Replay);
		if (!Section.IsValid())
		{
			continue;
		}
		AddSection(Section);
		if (FLoopSection* Loop = Section->AsLoop())
		{
			Loop->SetSplineExtent(Record.SplineExtent);
		}
		Recalculate();
	}

	for (const FTrackSpan& Span : Layout.FlatSweeps)
	{
		AddFlatSweep(Span.Start, Span.End, Span.OuterSide);
	}
	for (const FTrackSpan& Span : Layout.OpenStretches)
	{
		AddOpenStretch(Span.Start, Span.End);
	}
	if (Layout.EndZoneStart >= 0.f)
	{
		SetEndZone(Layout.EndZoneStart);
	}
	SetEnd(Layout.EndDistance);

	if (FMath::Abs(Length - Layout.Length) > 1.f)
	{
		UE_LOG(LogRunnerTrack, Warning, TEXT("Saved track rebuilt to %.1f m but was saved at %.1f m."), Length, Layout.Length);
		return false;
	}
	return true;
}
