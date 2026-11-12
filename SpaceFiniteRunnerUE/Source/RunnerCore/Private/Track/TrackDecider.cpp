#include "Track/TrackDecider.h"

#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include <limits>
#include "TrackContracts.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/RunnerSpline.h"
#include "Track/Spawning/TrackSpawner.h"
#include "Track/Spawning/WeightedTable.h"
#include "Track/TrackLayoutAsset.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"
#include "Track/TrackShape.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunnerDecide, Log, All);

namespace
{
	/** float.MaxValue: a cursor that never comes. */
	constexpr float Never = TNumericLimits<float>::Max();

	/** AutoSmooth reshapes the curves round the previous knot whenever one lands: the last two segments are never final. */
	constexpr float SettleMargin = 2.f * UTrackDecider::SegmentMax;

	/** Most knots a track may take before the decide gives up (a 33 km track takes about a hundred). */
	constexpr int32 MaxKnots = 5000;

	/** Mathf.Sign: 1 for zero. */
	float UnitySign(float V) { return V >= 0.f ? 1.f : -1.f; }

	/** Mathf.MoveTowards. */
	float MoveTowards(float Current, float Target, float MaxDelta)
	{
		if (FMath::Abs(Target - Current) <= MaxDelta)
		{
			return Target;
		}
		return Current + FMath::Sign(Target - Current) * MaxDelta;
	}

	/** Mathf.RoundToInt: half to even, as Math.Round. */
	int32 RoundToInt(float V) { return static_cast<int32>(FMath::RoundHalfToEven(V)); }

	float FeatureProbability(const FTrackFeatureEntry& E) { return E.Probability; }

	/** Records the spawn set decides: what a reroll replaces. */
	bool IsRerolled(const FTrackPlacement& P)
	{
		switch (P.Kind)
		{
		case ETrackPlacementKind::SpeedOrb:
		case ETrackPlacementKind::RepairOrb:
		case ETrackPlacementKind::LaserGate:
		case ETrackPlacementKind::Collectible:
		case ETrackPlacementKind::BrakePad:
			return true;
		default:
			return false;
		}
	}

	template <typename T>
	T* RuntimeCopy(const T* Source, UObject* Outer)
	{
		return Source != nullptr
			? DuplicateObject<T>(Source, Outer, MakeUniqueObjectName(Outer, Source->GetClass(), Source->GetFName()))
			: nullptr;
	}
}

// =================================================================== setup

void UTrackDecider::Bind(IStreamFocus* InFocus, IShipPerformance* InPerformance, const ITrackRunRules* InRules)
{
	Focus = InFocus;
	Performance = InPerformance;
	Rules = InRules;
}

void UTrackDecider::PrepareCopies(const UTrackShape* InShape, const UTrackSpawnSet* InSpawnSet, const UJumpDefinition* InEndRamp)
{
	// Gameplay never writes a DataAsset: the run plays copies, straight from the assets.
	Shape = InShape != nullptr ? RuntimeCopy(InShape, this) : NewObject<UTrackShape>(this);
	for (FTrackFeatureEntry& Entry : Shape->FeatureTable)
	{
		Entry.Definition = RuntimeCopy(Entry.Definition.Get(), this);
	}

	Spawners.Reset();
	if (InSpawnSet != nullptr)
	{
		for (const TObjectPtr<UTrackSpawner>& Spawner : InSpawnSet->Spawners)
		{
			Spawners.Add(RuntimeCopy(Spawner.Get(), this));
		}
	}

	EndRamp = RuntimeCopy(InEndRamp, this);
	if (EndRamp == nullptr)
	{
		// Unity's built-in end ramp numbers, when neither the level nor the project has a definition.
		EndRamp = NewObject<UJumpDefinition>(this);
		EndRamp->DisplayName = TEXT("End");
		EndRamp->Length = 120.f;
		EndRamp->RampAngle = 15.f;
		EndRamp->EntryMargin = 0.f;
		EndRamp->SideHitSpeedLoss = 0.05f;
	}
}

void UTrackDecider::ResetState(const FTrackDecideParams& Params)
{
	if (Track == nullptr)
	{
		Track = NewObject<UTrackModel>(this);
	}
	Track->ClearKnots();

	LastSeed = Params.Seed != 0u ? Params.Seed : (FPlatformTime::Cycles() | 1u);
	Rng = FRunnerRandom(LastSeed);
	// Every spawner (and the ramps) seeds a stream of its own off the layout stream's STATE, not a
	// draw from it: the road never depends on what spawns.
	RampRng = FRunnerRandom(UTrackSpawner::Hash2(Rng.State, UTrackSpawner::NameHash(TEXT("JumpRamps"))) | 1u);

	Heading = 0.f;
	Pitch = 0.f;
	Bank = 0.f;
	TurnKnotsLeft = 0;
	TurnRate = 0.f;
	StraightKnots = 0;
	LastTurnSign = 0.f;
	bSweepFlat = false;
	DeferredFlatKnots = 0;
	FlatSweepIndex = INDEX_NONE;
	bFlatSweepGrew = false;
	LastKnotDistance = 0.f;
	bLastKnotStraight = false;
	bStraightRunRolled = false;
	bStraightRunOpen = false;
	OpenStretchIndex = INDEX_NONE;
	EndPosition = FVector3f::ZeroVector;

	PendingRamps.Reset();
	StraightUntil = 0.f;
	FeatureCursor = 0.f;
	bInEndZone = false;
	bComplete = false;
	bLoaded = false;
	KnotDistances.Reset();
	SectionRecords.Reset();
	PlacementsTaken = 0;
}

void UTrackDecider::BeginContext(const FTrackDecideParams& Params)
{
	Ctx.Reset();
	Ctx.Track = Track;
	Ctx.Rules = Rules;
	Ctx.PadSize = Params.PadSize;
	Ctx.LayoutSeed = FRunnerRandom(LastSeed).State;
	Ctx.RoadSurfaceOffset = Params.RoadSurfaceOffset;
	IStreamFocus* BoundFocus = Focus;
	Ctx.FocusSource = [BoundFocus]() { return BoundFocus; };
	Ctx.World = Params.World;
	Ctx.Parent = Params.Parent;
	for (UTrackSpawner* Spawner : Spawners)
	{
		if (Spawner != nullptr) Spawner->Begin(Ctx);
	}
}

void UTrackDecider::Generate(const UTrackShape* InShape, const UTrackSpawnSet* SpawnSet, const UJumpDefinition* InEndRamp, const FTrackDecideParams& Params)
{
	PrepareCopies(InShape, SpawnSet, InEndRamp);
	ResetState(Params);
	Track->SetWidth(FMath::Clamp(Shape->TrackWidth, 10.f, 120.f));

	Track->AppendKnot(FVector::ZeroVector);
	KnotDistances.Add(0.f);
	// The spline was just replaced: without this, Length still reports the last track.
	Track->Recalculate();

	BeginContext(Params);
	FeatureCursor = Rng.NextFloat(FMath::Max(100.f, Shape->GetFeatureSpacingMin()), FMath::Max(100.f, Shape->GetFeatureSpacingMax()));

	TargetLength = Params.TargetLength > 0.f ? Params.TargetLength
		: Rules != nullptr && Rules->GetTrackLengthMeters() > 0.f ? Rules->GetTrackLengthMeters()
		: DefaultTrackLength;
	EndRunUp = Rules != nullptr ? Rules->GetEndRunUpMeters() : DefaultEndRunUp;
	// Never a run-up that eats the whole track.
	EndRunUp = FMath::Min(EndRunUp, TargetLength * 0.5f);
	EndZoneTarget = TargetLength - EndRunUp;
	EndTarget = TargetLength;

	Decide(TNumericLimits<float>::Max());
}

bool UTrackDecider::Load(const UTrackLayoutAsset* Asset, const UTrackSpawnSet* FallbackSpawnSet, const UJumpDefinition* InEndRamp, const FTrackDecideParams& Params)
{
	if (Asset == nullptr || !Asset->IsValid())
	{
		UE_LOG(LogRunnerDecide, Warning, TEXT("Saved track '%s' is empty or from an older format."), Asset != nullptr ? *Asset->GetName() : TEXT("null"));
		return false;
	}

	const FTrackLayout& Layout = Asset->Layout;
	PrepareCopies(Asset->Shape, Asset->SpawnSet != nullptr ? Asset->SpawnSet.Get() : FallbackSpawnSet, InEndRamp);
	FTrackDecideParams LoadParams = Params;
	LoadParams.Seed = static_cast<uint32>(Layout.Seed);
	ResetState(LoadParams);
	LastSeed = static_cast<uint32>(Layout.Seed); // even a zero seed: it is what the asset says

	const bool bRoadMatches = Track->LoadRoad(Layout, Shape);
	BeginContext(LoadParams);

	TargetLength = Layout.EndDistance;
	EndRunUp = FMath::Min(Layout.EndRunUp, TargetLength * 0.5f);
	EndZoneTarget = Layout.EndZoneStart;
	EndTarget = TargetLength;

	Ctx.FeatureKeepOuts.Append(Layout.KeepOuts);
	Ctx.Placements.Append(Layout.Placements);
	for (UTrackSpawner* Spawner : Spawners)
	{
		if (Spawner != nullptr) Spawner->Park(); // nothing more is decided on a loaded track
	}
	FeatureCursor = Never;
	bInEndZone = true;
	bComplete = true;
	bLoaded = true;
	KnotDistances = Layout.KnotDistances;
	SectionRecords = Layout.Sections;
	return bRoadMatches;
}

void UTrackDecider::RedecideSpawnSet(uint32 Seed)
{
	if (Track == nullptr || !bComplete)
	{
		return;
	}
	const uint32 RerollSeed = Seed != 0u ? Seed : (FPlatformTime::Cycles() | 1u);
	Ctx.LayoutSeed = FRunnerRandom(RerollSeed).State;

	// What stays (ramps, loops, the run-up, catalog power-ups) is claimed first, so everything
	// decided keeps off it by the usual rules.
	Ctx.Placements.RemoveAll(IsRerolled);
	Ctx.Claims.Reset();
	Ctx.PickupDistances.Reset();
	for (const FTrackPlacement& P : Ctx.Placements)
	{
		if (P.Kind == ETrackPlacementKind::Ramp)
		{
			if (const FTrackFeatureEntry* Entry = FeatureEntry(P.Variant); Entry != nullptr && Entry->Definition != nullptr)
			{
				Ctx.Claim(P.Distance, P.Distance + Entry->Definition->GetFootprintLength());
			}
		}
		if (P.Kind == ETrackPlacementKind::Pickup)
		{
			Ctx.RecordPickup(P.Distance);
		}
	}
	for (const TSharedPtr<FTrackSection>& Section : Track->GetSections())
	{
		if (Section->AsLoop() != nullptr) Ctx.Claim(Section->GetStartDistance(), Section->GetEndDistance());
	}
	if (Track->GetEndZoneStart() >= 0.f)
	{
		Ctx.Claim(Track->GetEndZoneStart(), Track->GetEndDistance() + 1000.f);
	}

	for (UTrackSpawner* Spawner : Spawners)
	{
		if (Spawner != nullptr) Spawner->Begin(Ctx);
	}
	PlaceSpawnersUpTo(TrackLength()); // complete: no stage guard
	PlacementsTaken = 0;              // the builder re-queues every record
}

// ================================================================== decide

float UTrackDecider::TrackLength() const
{
	return Track != nullptr ? Track->GetLength() : 0.f;
}

bool UTrackDecider::HasFeatures() const
{
	return Shape != nullptr && Shape->FeatureTable.Num() > 0;
}

const FTrackFeatureEntry* UTrackDecider::FeatureEntry(int32 Index) const
{
	return Shape != nullptr && Shape->FeatureTable.IsValidIndex(Index) ? &Shape->FeatureTable[Index] : nullptr;
}

float UTrackDecider::DecidedDistance() const
{
	return bComplete ? TrackLength() : FMath::Max(0.f, TrackLength() - SettleMargin);
}

float UTrackDecider::GetEndDistance() const
{
	return Track != nullptr && Track->HasEnd() ? Track->GetEndDistance() : TargetLength;
}

void UTrackDecider::Decide(float Target)
{
	// A knot that lands on the next feature spot decides the feature there and then, and the spline
	// continues from it; the end spots finish the track.
	int32 Guard = 0;
	while (!bComplete && TrackLength() - SettleMargin < Target)
	{
		if (++Guard > MaxKnots)
		{
			UE_LOG(LogRunnerDecide, Error, TEXT("Track decide gave up after %d knots at %.0f m (target %.0f m)."), MaxKnots, TrackLength(), TargetLength);
			break;
		}
		const ESpot Spot = AddSegment();
		Track->Recalculate();
		KnotDistances.Add(TrackLength());
		GrowFlatSweep();
		switch (Spot)
		{
		case ESpot::Feature: DecideFeature(); break;
		case ESpot::EndZoneStart: BeginEndZone(); break;
		case ESpot::End: FinishTrack(); break;
		default: break;
		}
	}
	DecidePlacementsUpTo(DecidedDistance());
}

void UTrackDecider::DecidePlacementsUpTo(float Limit)
{
	// Ramps first (their footprint is already claimed), then the spawn set: ground claimers, then
	// pickups in set order.
	SpawnPendingRamps(Limit);
	PlaceSpawnersUpTo(Limit);
}

UTrackDecider::ESpot UTrackDecider::AddSegment()
{
	const UTrackShape& S = *Shape;
	const float Here = TrackLength();

	// No feature spot is left that the final run-up would swallow.
	if (IsFinite() && !bInEndZone && FeatureCursor >= EndZoneTarget - SegmentMax)
	{
		FeatureCursor = Never;
	}

	float Remaining = FeatureCursor - Here;
	ESpot Spot = HasFeatures() && Remaining <= SegmentMax ? ESpot::Feature : ESpot::None;

	// The end spots: the nearer of the feature spot and the end spot is the one this knot may land on.
	bool bSplitToEnd = false;
	if (IsFinite())
	{
		const float ToEnd = (bInEndZone ? EndTarget : EndZoneTarget) - Here;
		if (Spot == ESpot::None || ToEnd <= Remaining)
		{
			if (ToEnd <= SegmentMax)
			{
				Spot = bInEndZone ? ESpot::End : ESpot::EndZoneStart;
				Remaining = ToEnd;
			}
			else if (ToEnd < SegmentMin + SegmentMax)
			{
				// Too far for one segment, too near for two full ones: split the rest evenly.
				Spot = ESpot::None;
				Remaining = ToEnd;
				bSplitToEnd = true;
			}
		}
	}
	const bool bLandOnSpot = Spot != ESpot::None;

	// Turns are SWEEPS: one direction at one rate for as many knots as the arc needs, then straight
	// for a few. Straightness is the chance a straight knot starts one; one draw per straight knot.
	const float Curviness = 1.f - FMath::Clamp(S.Straightness, 0.f, 100.f) / 100.f;
	const float PreviousHeading = Heading;
	// A ramp's landing zone (and the final run-up): straight, level and flat.
	const bool bHoldStraight = Here < StraightUntil || bInEndZone;
	if (TurnKnotsLeft == 0)
	{
		const float Roll = Rng.NextFloat(0.f, 1.f);
		++StraightKnots;
		if (DeferredFlatKnots > 0)
		{
			// A flat sweep waiting for level road: dropped if the road ahead was claimed meanwhile.
			if (bHoldStraight || !TurnFits(true))
			{
				DeferredFlatKnots = 0;
			}
			else if (RunnerMath::Approximately(Bank, 0.f))
			{
				TurnKnotsLeft = DeferredFlatKnots;
				DeferredFlatKnots = 0;
				bSweepFlat = true;
				LastTurnSign = UnitySign(TurnRate);
			}
		}
		else if (!bHoldStraight && Roll < Curviness && StraightKnots > S.MinStraightKnots)
		{
			StartTurn();
		}
	}
	if (TurnKnotsLeft > 0)
	{
		if (bHoldStraight || LevelRequired())
		{
			// A feature (or a landing) is coming: end the sweep now so the bank unwinds.
			TurnKnotsLeft = 0;
			StraightKnots = 0;
		}
		else
		{
			Heading += TurnRate;
			if (--TurnKnotsLeft == 0) StraightKnots = 0;
		}
	}
	const float TurnDelta = Heading - PreviousHeading;

	// A flat sweep's span opens at the previous knot (AutoSmooth bends the segment before a heading
	// change too) and is pushed out to each new knot once its distance is known (GrowFlatSweep).
	if (TurnDelta != 0.f && bSweepFlat)
	{
		if (FlatSweepIndex == INDEX_NONE)
		{
			FlatSweepIndex = Track->AddFlatSweep(LastKnotDistance, Here, TurnDelta > 0.f ? -1 : 1);
		}
		bFlatSweepGrew = true;
	}
	else
	{
		FlatSweepIndex = INDEX_NONE;
	}

	// Open straights: a segment is plain straight road when the knots at BOTH its ends are. That is
	// only known as the knot after it is laid, so the segment behind the end knot is opened here.
	const bool bStraightKnot = TurnDelta == 0.f && TurnKnotsLeft == 0 && DeferredFlatKnots == 0
		&& RunnerMath::Approximately(Bank, 0.f) && !bHoldStraight && !bLandOnSpot
		&& !LevelRequired() && Track->SectionAt(Here) == nullptr;
	if (bStraightKnot && bLastKnotStraight)
	{
		if (!bStraightRunRolled)
		{
			bStraightRunRolled = true;
			bStraightRunOpen = S.OpenStraightChance > 0.f && Rng.NextFloat(0.f, 1.f) < S.OpenStraightChance;
		}
		if (bStraightRunOpen)
		{
			if (OpenStretchIndex != INDEX_NONE) Track->SetOpenStretchEnd(OpenStretchIndex, Here);
			else OpenStretchIndex = Track->AddOpenStretch(LastKnotDistance, Here);
		}
	}
	else if (!bStraightKnot)
	{
		bStraightRunRolled = false;
		OpenStretchIndex = INDEX_NONE;
	}
	bLastKnotStraight = bStraightKnot;
	LastKnotDistance = Here;

	// Elevation: a grade walk inside a band round the baseline. Under a landing the grade holds.
	// Off, it draws nothing, so a seed reproduces the flat track exactly.
	if (bHoldStraight)
	{
	}
	else if (S.bElevationEnabled && S.MaxGrade > 0.f)
	{
		const float Step = S.MaxGradeStepPerKnot;
		const float Band = FMath::Max(S.ElevationBand, 1.f);
		const float Y = EndPosition.Z;
		Pitch += Rng.NextFloat(-Step, Step);
		Pitch -= S.BaselinePull * (Y / Band) * Step;
		if (FMath::Abs(Y) > Band) Pitch = -UnitySign(Y) * FMath::Max(FMath::Abs(Pitch), Step);
		Pitch = FMath::Clamp(Pitch, -S.MaxGrade, S.MaxGrade);
	}
	else
	{
		Pitch = 0.f;
	}

	// Banking: lean into the turn (a right turn drops the right edge), eased per knot, level wherever
	// a feature is coming. No draws.
	float BankTarget = 0.f;
	if (S.bBankEnabled && S.MaxBankAngle > 0.f && !bHoldStraight && !bSweepFlat && !LevelRequired())
	{
		BankTarget = FMath::Clamp(-TurnDelta * S.BankPerDegreeOfTurn, -S.MaxBankAngle, S.MaxBankAngle);
	}
	Bank = S.bBankEnabled ? MoveTowards(Bank, BankTarget, FMath::Max(S.MaxBankStepPerKnot, 0.01f)) : 0.f;

	// Unity's (sin yaw cos g, sin g, cos yaw cos g) in Unreal axes.
	const float Yaw = FMath::DegreesToRadians(Heading);
	const float Grade = FMath::DegreesToRadians(Pitch);
	const FVector3f Direction(FMath::Cos(Yaw) * FMath::Cos(Grade), FMath::Sin(Yaw) * FMath::Cos(Grade), FMath::Sin(Grade));
	float Chord = bLandOnSpot
		? FMath::Max(Remaining, Spot == ESpot::Feature ? SegmentMin : SegmentMin * 0.5f)
		: Rng.NextFloat(SegmentMin, SegmentMax);
	if (bSplitToEnd) Chord = Remaining * 0.5f;
	if (bLandOnSpot) Bank = 0.f; // a feature's entry is level, full stop
	EndPosition += Direction * Chord;

	// The knot carries heading, grade and bank: AutoSmooth keeps the authored up.
	FQuat4f Rotation = RunnerMath::LookRotation(Direction, RunnerMath::UnitUp);
	if (Bank != 0.f) Rotation = FQuat4f(Direction, FMath::DegreesToRadians(Bank)) * Rotation;
	if (bLandOnSpot)
	{
		Track->AppendKnot(FVector(EndPosition), FQuat(Rotation), FVector(Direction * (Chord / 3.f))); // pinned: the feature's entry pose
	}
	else
	{
		Track->AppendKnot(FVector(EndPosition), FQuat(Rotation));
	}
	return Spot;
}

void UTrackDecider::GrowFlatSweep()
{
	if (!bFlatSweepGrew) return;
	bFlatSweepGrew = false;
	if (FlatSweepIndex != INDEX_NONE) Track->SetFlatSweepEnd(FlatSweepIndex, TrackLength());
}

bool UTrackDecider::LevelRequired() const
{
	const float End = TrackLength();
	if (const FTrackSection* Section = Track->SectionAt(End); Section != nullptr && Section->AsTube() != nullptr)
	{
		return true;
	}
	const float Unwind = FMath::CeilToFloat(FMath::Abs(Bank) / FMath::Max(Shape->MaxBankStepPerKnot, 0.01f)) * SegmentMax;
	return NextLevelSpot() - End <= Shape->LevelLeadDistance + Unwind;
}

float UTrackDecider::NextLevelSpot() const
{
	return IsFinite() && !bInEndZone ? FMath::Min(FeatureCursor, EndZoneTarget) : FeatureCursor;
}

bool UTrackDecider::TurnFits(bool bFlat) const
{
	const float End = TrackLength();
	if (End < StraightUntil) return false; // a ramp's landing zone
	if (const FTrackSection* Section = Track->SectionAt(End); Section != nullptr && Section->AsTube() != nullptr) return false;
	const float SweepKnots = FMath::CeilToFloat(Shape->GetTurnArcMin() / Shape->GetTurnRateMax());
	const float UnwindKnots = Shape->bBankEnabled && !bFlat
		? FMath::CeilToFloat(Shape->MaxBankAngle / FMath::Max(Shape->MaxBankStepPerKnot, 0.01f))
		: 0.f;
	const float Needed = (SweepKnots + UnwindKnots) * SegmentMax + Shape->LevelLeadDistance;
	return NextLevelSpot() - End > Needed;
}

void UTrackDecider::StartTurn()
{
	const UTrackShape& S = *Shape;
	// Nothing is drawn for a sweep that cannot fit either way.
	const bool bBankedFits = TurnFits(false);
	const bool bFlatPossible = S.UnbankedSweepChance > 0.f && TurnFits(true);
	if (!bBankedFits && !bFlatPossible) return;

	const float Rate = Rng.NextFloat(S.GetTurnRateMin(), S.GetTurnRateMax());
	const float Arc = Rng.NextFloat(S.GetTurnArcMin(), S.GetTurnArcMax());
	float Sign;
	if (FMath::Abs(Heading) > S.MaxHeadingDrift) Sign = -UnitySign(Heading);
	else if (LastTurnSign == 0.f) Sign = Rng.NextFloat(0.f, 1.f) < 0.5f ? -1.f : 1.f;
	else Sign = Rng.NextFloat(0.f, 1.f) < S.AlternateTurnChance ? -LastTurnSign : LastTurnSign;

	const int32 Knots = FMath::Max(1, RoundToInt(Arc / Rate));

	// Flat or banked? One draw per sweep, none at chance 0.
	const bool bFlat = bFlatPossible && Rng.NextFloat(0.f, 1.f) < S.UnbankedSweepChance;
	if (!bFlat && !bBankedFits) return; // only a flat sweep fitted, and the roll said banked

	TurnRate = Sign * Rate;
	bSweepFlat = false;
	if (bFlat && !RunnerMath::Approximately(Bank, 0.f))
	{
		// A flat sweep stands on level road: it waits while the last bank unwinds.
		DeferredFlatKnots = Knots;
		return;
	}
	TurnKnotsLeft = Knots;
	bSweepFlat = bFlat;
	LastTurnSign = Sign;
}

float UTrackDecider::DesignJumpStrength(const UJumpDefinition& Jump)
{
	return FMath::Max(1.f, Jump.DesignJumpStrength);
}

void UTrackDecider::DecideFeature()
{
	const float Spot = TrackLength(); // the knot that just landed, at its true track distance
	FeatureCursor = Spot;
	const float SpacingMin = FMath::Max(100.f, Shape->GetFeatureSpacingMin());
	const float SpacingMax = FMath::Max(SpacingMin, Shape->GetFeatureSpacingMax());
	if (!HasFeatures())
	{
		FeatureCursor = Spot + Rng.NextFloat(SpacingMin, SpacingMax);
		return;
	}

	// One weighted draw; a loop the ship cannot reach is redrawn among the others off the SAME roll.
	const TArray<FTrackFeatureEntry>& Table = Shape->FeatureTable;
	const float Roll = Rng.NextFloat(0.f, 1.f);
	int32 Index = FWeightedTable::Pick(Table, FeatureProbability, Roll);
	if (Index != INDEX_NONE && !LoopReachable(Table[Index], Spot))
	{
		Index = FWeightedTable::Pick(Table, FeatureProbability, Roll, Index);
	}
	if (Index == INDEX_NONE || Table[Index].Definition == nullptr)
	{
		FeatureCursor = Spot + Rng.NextFloat(SpacingMin, SpacingMax);
		return;
	}
	const FTrackFeatureEntry& Entry = Table[Index];
	const UTrackFeatureDefinition* Definition = Entry.Definition;

	const uint32 SectionState = Rng.State; // a saved track replays the section's rolls from here
	TSharedPtr<FTrackSection> Section = Definition->CreateSection(*Track, Spot, Rng);
	const float Footprint = Section.IsValid() ? Section->GetLength() : Definition->GetFootprintLength();

	// A ramp lands only where the road stays straight, level and flat for the longest jump anyone
	// can make plus a landing clearance: reserved ahead (StraightUntil), never checked.
	float Exclusion = Definition->GetExclusionAhead();
	const UJumpDefinition* Jump = Cast<UJumpDefinition>(Definition);
	if (Jump != nullptr) Exclusion = Jump->MaxAirDistance(DesignJumpStrength(*Jump)) + Jump->LandingClearance;

	// Nothing may reach into the final run-up: the spot is skipped (the section never registered).
	if (IsFinite() && Spot + Footprint + Exclusion > EndZoneTarget)
	{
		FeatureCursor = Spot + Rng.NextFloat(SpacingMin, SpacingMax);
		return;
	}

	if (Definition->ClaimsFootprint()) Ctx.Claim(Spot, Spot + Footprint);
	// Laser gates keep off EVERY feature (a tube too, which claims nothing) and what lies ahead of it.
	Ctx.FeatureKeepOuts.Add(FFloatInterval(Spot, Spot + Footprint + Exclusion));
	if (Jump != nullptr) StraightUntil = Spot + Jump->Length + Exclusion;

	FTrackSectionRecord Record;
	Record.FeatureIndex = Index;
	Record.Start = Spot;
	Record.RngState = static_cast<int64>(SectionState);
	if (FLoopSection* Loop = Section.IsValid() ? Section->AsLoop() : nullptr)
	{
		Track->AddSection(Section);
		ContinueFromLoopExit(*Loop);
		Record.SplineExtent = Loop->GetSplineExtent();
		SectionRecords.Add(Record);

		FTrackPlacement Placement;
		Placement.Kind = ETrackPlacementKind::Loop;
		Placement.Distance = Spot;
		Placement.Variant = Index;
		Ctx.Emit(Placement);
	}
	else if (Section.IsValid())
	{
		// A tube: the section is the whole feature; the spline keeps coming underneath it, level.
		Track->AddSection(Section);
		Record.SplineExtent = Section->GetSplineExtent();
		SectionRecords.Add(Record);
	}
	else
	{
		// A ramp is road-bound: it lands once its run-up is settled.
		PendingRamps.Emplace(Index, Spot);
	}

	FeatureCursor = Spot + Footprint + Exclusion + FMath::Max(Rng.NextFloat(SpacingMin, SpacingMax), Entry.MinSpacing);
}

bool UTrackDecider::LoopReachable(const FTrackFeatureEntry& Entry, float Distance) const
{
	if (Focus == nullptr || Performance == nullptr || Rules == nullptr) return true;
	const ULoopDefinition* Loop = Cast<ULoopDefinition>(Entry.Definition);
	if (Loop == nullptr) return true;

	// The track is decided whole at run start: predict from at least the cruise speed the throttle holds.
	const float Cruise = Performance->GetCruiseSpeed();
	const float Speed = FMath::Max(Focus->GetStreamSpeed(), Cruise);
	const float Gap = FMath::Max(0.f, Distance - Focus->GetStreamDistance());
	const float Bled = Speed - Performance->GetPassiveDeceleration() * Gap / FMath::Max(Speed, 1.f);
	const float Predicted = FMath::Max(Bled, FMath::Min(Speed, Cruise));
	const float Required = Rules->GetLoopRequiredSpeed(Distance) * (1.f + FMath::Max(0.f, Loop->GateHeadroom));
	return Predicted >= Required;
}

void UTrackDecider::ContinueFromLoopExit(FLoopSection& Loop)
{
	// The spline continues from the loop's exit: a pinned knot at the exit pose, the bridge between
	// the entry and exit knots measured as the section's spline extent, the builder re-read off the exit.
	FVector ExitPosition;
	FQuat ExitRotation;
	Loop.GetExitPose(0.f, ExitPosition, ExitRotation);
	const FVector3f ExitForward = FQuat4f(ExitRotation).RotateVector(RunnerMath::UnitForward);
	const float BridgeChord = FMath::Max(FVector3f::Distance(EndPosition, FVector3f(ExitPosition)), 1.f);

	const float Before = Track->GetSplineLength();
	Track->AppendKnot(ExitPosition, ExitRotation, FVector(ExitForward * (BridgeChord / 3.f)));
	Track->Recalculate();
	Loop.SetSplineExtent(Track->GetSplineLength() - Before);
	Track->Recalculate();
	KnotDistances.Add(TrackLength());

	EndPosition = FVector3f(ExitPosition);
	Heading = FMath::RadiansToDegrees(FMath::Atan2(ExitForward.Y, ExitForward.X));
	Pitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(ExitForward.Z, -1.f, 1.f)));
	Bank = 0.f;
	TurnKnotsLeft = 0;
	StraightKnots = 0;
}

// =============================================================== track end

void UTrackDecider::BeginEndZone()
{
	// From here every knot is collinear (straight, grade held, no bank) and nothing is placed.
	bInEndZone = true;
	const float Start = TrackLength();
	Track->SetEndZone(Start);
	EndTarget = Start + EndRunUp;
	Ctx.Claim(Start, EndTarget + 1000.f);
	Ctx.FeatureKeepOuts.Add(FFloatInterval(Start, std::numeric_limits<float>::infinity()));
	FeatureCursor = Never;
}

void UTrackDecider::FinishTrack()
{
	bComplete = true;
	Track->SetEnd(TrackLength());
	CreateEndRamps();

	// Each knot's distance was recorded as it landed, but the next knot re-derived its AutoSmooth
	// tangent and moved the curve before it (up to a metre on a sweep; Unity saves the stale values).
	// Now that nothing moves any more, every knot takes the distance the model gives it.
	for (int32 i = 0; i < KnotDistances.Num(); ++i)
	{
		KnotDistances[i] = Track->KnotTrackDistance(i);
	}
}

void UTrackDecider::CreateEndRamps()
{
	if (EndRamp == nullptr) return;
	float Width, SideGap;
	const float Gap = EndRampLayout(Width, SideGap);
	const float Start = Track->GetEndDistance() - EndRamp->Length;
	for (int32 I = -1; I <= 1; ++I)
	{
		float Half;
		FTrackPlacement Placement;
		Placement.Kind = ETrackPlacementKind::EndRamp;
		Placement.Distance = Start;
		Placement.Lateral = EndRampLateral(I, Width, Gap, Half);
		Placement.Variant = I + 1;
		Ctx.Emit(Placement);
	}
}

float UTrackDecider::EndRampLayout(float& OutWidth, float& OutSideGap) const
{
	const float Gap = Rules != nullptr ? Rules->GetEndRampGapMeters() : DefaultEndRampGap;
	OutSideGap = Rules != nullptr ? Rules->GetEndRampSideGapMeters() : 0.f;
	const float HalfWidth = Track != nullptr ? Track->GetHalfWidth() : 0.f;
	OutWidth = FMath::Max(2.f, (HalfWidth * 2.f - 2.f * Gap - 2.f * OutSideGap) / 3.f);
	return Gap;
}

float UTrackDecider::EndRampLateral(int32 I, float Width, float Gap, float& OutHalf) const
{
	float Lateral = I * (Width + Gap);
	OutHalf = Width * 0.5f;
	float IgnoredWidth, SideGap;
	EndRampLayout(IgnoredWidth, SideGap);
	if (I != 0 && SideGap <= 0.f)
	{
		// The outer ramps reach a little past the wall, so a ship pressed against it is on the ramp.
		OutHalf += 0.25f;
		Lateral += I * 0.25f;
	}
	return Lateral;
}

float UTrackDecider::GetEndRampLength() const
{
	return EndRamp != nullptr ? EndRamp->Length : 120.f;
}

float UTrackDecider::GetEndRampHalfWidth() const
{
	float Width, SideGap, Half;
	const float Gap = EndRampLayout(Width, SideGap);
	EndRampLateral(0, Width, Gap, Half);
	return Half;
}

TArray<float> UTrackDecider::EndRampLaterals() const
{
	float Width, SideGap, Half;
	const float Gap = EndRampLayout(Width, SideGap);
	return { EndRampLateral(-1, Width, Gap, Half), EndRampLateral(0, Width, Gap, Half), EndRampLateral(1, Width, Gap, Half) };
}

// ============================================================== placements

void UTrackDecider::SpawnPendingRamps(float Limit)
{
	// In road order: each ramp draws its lateral as it is decided.
	for (int32 i = 0; i < PendingRamps.Num();)
	{
		const int32 Index = PendingRamps[i].Key;
		const float Distance = PendingRamps[i].Value;
		const FTrackFeatureEntry* Entry = FeatureEntry(Index);
		const UTrackFeatureDefinition* Definition = Entry != nullptr ? Entry->Definition.Get() : nullptr;
		if (Definition != nullptr && Distance + Definition->GetFootprintLength() > Limit)
		{
			++i;
			continue;
		}
		if (const UJumpDefinition* Jump = Cast<UJumpDefinition>(Definition))
		{
			DecideJump(Distance, Index, *Jump);
		}
		PendingRamps.RemoveAt(i);
	}
}

void UTrackDecider::DecideJump(float Distance, int32 FeatureIndex, const UJumpDefinition& Jump)
{
	const float RampHalf = Track->GetHalfWidth() * FMath::Clamp(Jump.WidthFraction, 0.f, 1.f);
	const float MaxLateral = FMath::Max(0.f, Track->GetHalfWidth() - RampHalf - 2.f);
	FTrackPlacement Placement;
	Placement.Kind = ETrackPlacementKind::Ramp;
	Placement.Distance = Distance;
	Placement.Lateral = RampRng.NextFloat(-MaxLateral, MaxLateral);
	Placement.Variant = FeatureIndex;
	Ctx.Emit(Placement);
}

float UTrackDecider::PlaceSpawnersUpTo(float Limit)
{
	// Ground claimers first (a gate claims its stretch, so no pickup lands in a beam), then every
	// pickup spawner in set order, each StageGuard short of the one before it.
	for (UTrackSpawner* Spawner : Spawners)
	{
		if (Spawner != nullptr && Spawner->GetPhase() == ETrackSpawnPhase::ClaimsGround) Spawner->PlaceUpTo(Ctx, Limit);
	}
	for (UTrackSpawner* Spawner : Spawners)
	{
		if (Spawner == nullptr || Spawner->GetPhase() == ETrackSpawnPhase::ClaimsGround) continue;
		Limit -= StageGuard();
		Spawner->PlaceUpTo(Ctx, Limit);
	}
	return Limit;
}

int32 UTrackDecider::TakeNewPlacements(TArray<FTrackPlacement>& Out)
{
	PlacementsTaken = FMath::Min(PlacementsTaken, Ctx.Placements.Num());
	const int32 Count = Ctx.Placements.Num() - PlacementsTaken;
	for (int32 i = PlacementsTaken; i < Ctx.Placements.Num(); ++i)
	{
		Out.Add(Ctx.Placements[i]);
	}
	PlacementsTaken = Ctx.Placements.Num();
	return Count;
}

// ================================================================= capture

FTrackLayout UTrackDecider::CaptureLayout() const
{
	FTrackLayout Layout;
	if (Track == nullptr) return Layout;
	Layout.Seed = static_cast<int64>(LastSeed);
	Layout.Width = Track->GetHalfWidth() * 2.f;
	Layout.Length = Track->GetLength();
	Layout.EndZoneStart = Track->GetEndZoneStart();
	Layout.EndDistance = Track->GetEndDistance();
	Layout.EndRunUp = EndRunUp;
	// The knots as handed in, not as AutoSmooth left them: replayed in order they rebuild the spline.
	Layout.Knots = Track->GetAppendedKnots();
	Layout.KnotDistances = KnotDistances;
	Layout.Sections = SectionRecords;
	Layout.FlatSweeps = Track->GetFlatSweeps();
	Layout.OpenStretches = Track->GetOpenStretches();
	Layout.KeepOuts = Ctx.FeatureKeepOuts;
	Layout.Placements = Ctx.Placements;
	return Layout;
}

void UTrackDecider::CullBehind(float MinDistance)
{
	if (MinDistance <= 0.f) return;
	Ctx.Claims.RemoveAll([MinDistance](const FFloatInterval& C) { return C.Max < MinDistance; });
	Ctx.PickupDistances.RemoveAll([MinDistance](float D) { return D < MinDistance; });
	Ctx.FeatureKeepOuts.RemoveAll([MinDistance](const FFloatInterval& K) { return K.Max < MinDistance; });
}

bool UTrackDecider::IsGroundClear(float From, float To) const
{
	for (const FFloatInterval& KeepOut : Ctx.FeatureKeepOuts)
	{
		if (To > KeepOut.Min && From < KeepOut.Max) return false;
	}
	return true;
}

FFloatInterval UTrackDecider::RampKeepOut(int32 FeatureIndex, float Distance) const
{
	const FTrackFeatureEntry* Entry = FeatureEntry(FeatureIndex);
	const UJumpDefinition* Jump = Entry != nullptr ? Cast<UJumpDefinition>(Entry->Definition) : nullptr;
	if (Jump == nullptr) return FFloatInterval(Distance, Distance);
	const float Exclusion = Jump->MaxAirDistance(DesignJumpStrength(*Jump)) + Jump->LandingClearance;
	return FFloatInterval(Distance, Distance + Jump->GetFootprintLength() + Exclusion);
}

// =============================================================== hyperspace

bool UTrackDecider::ForceEndAhead(float RunUpMeters, float BuiltTo)
{
	if (!IsFinite() || Track == nullptr) return false;
	if (bComplete || bInEndZone)
	{
		if (!CutBackForEnd(BuiltTo)) return false;
	}
	else if (EndZoneStartFromTip() >= EndZoneTarget)
	{
		return false;
	}

	const float Start = EndZoneStartFromTip();
	PendingRamps.Reset();
	DeferredFlatKnots = 0;
	FeatureCursor = Never;

	EndRunUp = FMath::Max(RunUpMeters, GetEndRampLength() + SegmentMin * 0.5f);
	EndZoneTarget = Start;
	TargetLength = Start + EndRunUp;
	Decide(TNumericLimits<float>::Max()); // lay the new end now: the autopilot aims at it
	return true;
}

float UTrackDecider::EndZoneStartFromTip() const
{
	float Start = TrackLength() + SegmentMin * 0.5f;
	const float Step = FMath::Max(Shape->MaxBankStepPerKnot, 0.01f);
	const float Unwind = FMath::CeilToFloat(FMath::Abs(Bank) / Step) * SegmentMax;
	if (Unwind > 0.f || TurnKnotsLeft > 0)
	{
		Start += TurnKnotsLeft * SegmentMax + Unwind + Shape->LevelLeadDistance;
	}
	if (const FTrackSection* Section = Track->SectionAt(TrackLength()))
	{
		Start = FMath::Max(Start, Section->GetEndDistance() + SegmentMin * 0.5f);
	}
	return Start;
}

bool UTrackDecider::CutBackForEnd(float BuiltTo)
{
	// The last knot kept is the one AFTER the first knot CutClearance past the built road (dropping
	// the knots after it reshapes the segment ending there), never one inside a section.
	const int32 Count = KnotDistances.Num();
	const int32 First = KnotDistances.IndexOfByPredicate([BuiltTo](float D) { return D >= BuiltTo + CutClearance; });
	if (First == INDEX_NONE) return false;
	int32 Keep = First + 1;
	while (Keep < Count && Track->SectionAt(KnotDistances[Keep - 1]) != nullptr) ++Keep;
	if (Keep >= Count - 1) return false;

	const float Cut = KnotDistances[Keep];
	const float ReshapedFrom = KnotDistances[Keep - 1];
	const float ZoneStart = Track->GetEndZoneStart() >= 0.f ? Track->GetEndZoneStart() : EndZoneTarget;
	const float KeptBank = Track->GetBankAtDistance(Cut);
	const float Unwind = FMath::CeilToFloat(FMath::Abs(KeptBank) / FMath::Max(Shape->MaxBankStepPerKnot, 0.01f)) * SegmentMax;
	const float NewStart = Cut + SegmentMin * 0.5f + (Unwind > 0.f ? Unwind + Shape->LevelLeadDistance : 0.f);
	if (NewStart >= ZoneStart) return false; // the real end is already that close

	const FTrackKnot Knot = Track->GetAppendedKnots()[Keep];
	Track->TruncateKnots(Keep + 1, Cut);
	KnotDistances.SetNum(Keep + 1);

	Ctx.Placements.RemoveAll([ReshapedFrom](const FTrackPlacement& P) { return P.Distance >= ReshapedFrom; });
	SectionRecords.RemoveAll([Cut](const FTrackSectionRecord& R) { return R.Start >= Cut; });
	PlacementsTaken = FMath::Min(PlacementsTaken, Ctx.Placements.Num());
	Ctx.Claims.RemoveAll([ReshapedFrom](const FFloatInterval& C) { return C.Min >= ReshapedFrom; });
	Ctx.FeatureKeepOuts.RemoveAll([ReshapedFrom](const FFloatInterval& K) { return K.Min >= ReshapedFrom; });
	Ctx.PickupDistances.RemoveAll([ReshapedFrom](float D) { return D >= ReshapedFrom; });

	ResumeBuilderAt(Knot.Position, Knot.Rotation, KeptBank, KnotDistances[Keep - 1]);
	return true;
}

void UTrackDecider::ResumeBuilderAt(const FVector& Position, const FQuat& Rotation, float KnotBank, float PreviousKnotDistance)
{
	const FVector3f Forward = FQuat4f(Rotation).RotateVector(RunnerMath::UnitForward);
	EndPosition = FVector3f(Position);
	Heading = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
	Pitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Forward.Z, -1.f, 1.f)));
	Bank = KnotBank;
	TurnKnotsLeft = 0;
	TurnRate = 0.f;
	StraightKnots = 0;
	bSweepFlat = false;
	DeferredFlatKnots = 0;
	FlatSweepIndex = INDEX_NONE;
	bFlatSweepGrew = false;
	LastKnotDistance = PreviousKnotDistance;
	bLastKnotStraight = false;
	bStraightRunRolled = false;
	OpenStretchIndex = INDEX_NONE;
	bInEndZone = false;
	bComplete = false;
}
