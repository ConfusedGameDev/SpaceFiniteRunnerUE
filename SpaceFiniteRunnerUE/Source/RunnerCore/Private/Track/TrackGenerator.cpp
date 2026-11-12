#include "Track/TrackGenerator.h"

#include "Algo/StableSort.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TrackContracts.h"
#include "Track/Features/JumpRamp.h"
#include "Track/Features/LoopGate.h"
#include "Track/Features/TrackFeatureDefinitions.h"
#include "Track/Spawning/TrackSpawner.h"
#include "Track/TrackColliderBuilder.h"
#include "Track/TrackDecider.h"
#include "Track/TrackDecorator.h"
#include "Track/TrackLayoutAsset.h"
#include "Track/TrackModel.h"
#include "Track/TrackSection.h"
#include "Track/TrackShape.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackGenerator, Log, All);

namespace
{
	const FName BaseColorParam(TEXT("BaseColor"));
	const FName EmissionColorParam(TEXT("EmissionColor"));
}

ATrackGenerator::ATrackGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
	Decorator = CreateDefaultSubobject<UTrackDecorator>(TEXT("Decorator"));
	Colliders = CreateDefaultSubobject<UTrackColliderBuilder>(TEXT("Colliders"));
	FeatureBoxMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	FeatureMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Runner/Materials/Lit/M_Lit.M_Lit")));
}

void ATrackGenerator::Bind(IStreamFocus* InFocus, IShipPerformance* InPerformance, const ITrackRunRules* InRules)
{
	Focus = InFocus;
	Performance = InPerformance;
	Rules = InRules;
	if (Decider) Decider->Bind(Focus, Performance, Rules);
}

UTrackModel* ATrackGenerator::GetTrack() const
{
	return Decider ? Decider->GetTrack() : nullptr;
}

void ATrackGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (bBuildOnBeginPlay) BuildTrack();
}

void ATrackGenerator::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearBuilt();
	Super::EndPlay(Reason);
}

void ATrackGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Focus && GetTrack()) StreamTo(Focus->GetStreamDistance());
}

// ------------------------------------------------------------------ build

bool ATrackGenerator::BuildTrack()
{
	ClearBuilt();
	if (!Decider) Decider = NewObject<UTrackDecider>(this, TEXT("Decider"));
	Decider->Bind(Focus, Performance, Rules);
	Box = FeatureBoxMesh.LoadSynchronous();
	BaseFeatureMaterial = FeatureMaterial.LoadSynchronous();

	FTrackDecideParams Params;
	Params.Seed = static_cast<uint32>(Seed);
	Params.PadSize = PadSize;
	Params.RoadSurfaceOffset = Decorator ? Decorator->RoadYOffset : -1.2f;
	Params.World = GetWorld();
	Params.Parent = this;

	const UTrackLayoutAsset* Saved = Rules ? Cast<UTrackLayoutAsset>(Rules->GetAuthoredTrack()) : nullptr;
	if (!Saved) Saved = Layout;

	bool bOk = true;
	if (Saved)
	{
		bOk = Decider->Load(Saved, SpawnSet, EndRamp, Params);
		if (!bOk && Decider->GetTrack() == nullptr) return false;
	}
	else
	{
		if (!Shape)
		{
			UE_LOG(LogTrackGenerator, Warning, TEXT("%s: no track shape and no saved track: nothing to lay."), *GetName());
			return false;
		}
		Decider->Generate(Shape, SpawnSet, EndRamp, Params);
	}

	UTrackModel* Model = Decider->GetTrack();
	if (!Model || Model->GetLength() <= 0.f) return false;
	Model->SetTrackToWorld(GetActorTransform());
	Decorator->SetTrack(Model);
	Colliders->SetTrack(Model);
	QueueNewPlacements();
	OnRegenerated.Broadcast();
	BuildUpTo(AheadDistance);
	return bOk;
}

void ATrackGenerator::ClearBuilt()
{
	if (Decider)
	{
		for (TPair<float, TWeakObjectPtr<AActor>>& Spawned : Decider->GetContext().Spawned)
		{
			if (AActor* Actor = Spawned.Value.Get()) Actor->Destroy();
		}
		Decider->GetContext().Spawned.Reset();
	}
	Ramps.Reset();
	Loops.Reset();
	Unbuilt.Reset();
	BuiltTo = 0.f;
	CulledTo = 0.f;
	if (Decorator) Decorator->Clear();
	if (Colliders) Colliders->Clear();
}

void ATrackGenerator::QueueNewPlacements()
{
	TArray<FTrackPlacement> Fresh;
	if (Decider->TakeNewPlacements(Fresh) == 0) return;
	Unbuilt.Append(Fresh);
	// Stable: records at one distance build in decide order.
	Algo::StableSortBy(Unbuilt, &FTrackPlacement::Distance);
}

void ATrackGenerator::StreamTo(float FocusDistance)
{
	BuildUpTo(FocusDistance + AheadDistance);
	CullBehind(FocusDistance - BehindDistance);
}

bool ATrackGenerator::ForceEndAhead(float RunUpMeters)
{
	if (!Decider || !Decider->ForceEndAhead(RunUpMeters, BuiltTo)) return false;
	// The road past the cut is new: what was queued there is gone, the new records come in.
	TArray<FTrackPlacement> Discard;
	Decider->TakeNewPlacements(Discard);
	Unbuilt.Reset();
	for (const FTrackPlacement& P : Decider->GetPlacements())
	{
		if (P.Distance >= BuiltTo) Unbuilt.Add(P);
	}
	Algo::StableSortBy(Unbuilt, &FTrackPlacement::Distance);
	return true;
}

void ATrackGenerator::BuildUpTo(float Edge)
{
	UTrackModel* Model = GetTrack();
	if (!Model) return;
	Edge = FMath::Min(Edge, Model->GetLength());
	if (Edge <= BuiltTo) return;
	int32 Count = 0;
	while (Count < Unbuilt.Num() && Unbuilt[Count].Distance < Edge)
	{
		Build(Unbuilt[Count++]);
	}
	Unbuilt.RemoveAt(0, Count, EAllowShrinking::No);
	BuiltTo = Edge;
	Decorator->DecorateUpTo(Edge);
	Colliders->BuildUpTo(Edge, Ramps);
}

void ATrackGenerator::CullBehind(float MinDistance)
{
	if (MinDistance <= CulledTo) return;
	CulledTo = MinDistance;
	TArray<TPair<float, TWeakObjectPtr<AActor>>>& Spawned = Decider->GetContext().Spawned;
	for (int32 i = Spawned.Num() - 1; i >= 0; --i)
	{
		if (Spawned[i].Key >= MinDistance) continue;
		if (AActor* Actor = Spawned[i].Value.Get()) Actor->Destroy();
		Spawned.RemoveAtSwap(i);
	}
	Ramps.RemoveAll([](const TObjectPtr<AJumpRamp>& R) { return !IsValid(R); });
	Loops.RemoveAll([](const TObjectPtr<ALoopGate>& L) { return !IsValid(L); });
	Decider->CullBehind(MinDistance);
	Decorator->CullBefore(MinDistance);
	Colliders->CullBefore(MinDistance);
}

void ATrackGenerator::Remember(AActor* Actor, float CullKey, const FTrackPlacement& Placement)
{
	if (!Actor) return;
	Decider->GetContext().Spawned.Emplace(CullKey, Actor);
	OnPlacementBuilt.Broadcast(Actor, Placement.Distance, Placement.Lateral);
}

void ATrackGenerator::Build(const FTrackPlacement& Placement)
{
	switch (Placement.Kind)
	{
	case ETrackPlacementKind::Ramp:
		BuildJump(Placement);
		return;
	case ETrackPlacementKind::Loop:
		BuildLoop(Placement);
		return;
	case ETrackPlacementKind::EndRamp:
		BuildEndRamp(Placement);
		return;
	case ETrackPlacementKind::Collectible:   // coins are not ported (PRD §3)
	case ETrackPlacementKind::Checkpoint:    // the time-trial system draws its gates
	case ETrackPlacementKind::BrakePad:      // retired
		return;
	default:
		break;
	}
	for (UTrackSpawner* Spawner : Decider->GetSpawners())
	{
		if (Spawner && Spawner->GetKind() == Placement.Kind)
		{
			// A spawner registers what it builds in the context itself, or returns it for us to.
			const int32 Before = Decider->GetContext().Spawned.Num();
			AActor* Actor = Spawner->Build(Decider->GetContext(), Placement);
			if (Actor && Decider->GetContext().Spawned.Num() == Before) Remember(Actor, Placement.Distance, Placement);
			return;
		}
	}
}

template <class T>
T* ATrackGenerator::SpawnOnTrack(float Distance, float Lateral)
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	const FTransform Pose = GetTrack()->GetWorldPose(Distance, Lateral);
	T* Actor = World->SpawnActor<T>(T::StaticClass(), Pose, SpawnParams);
	if (Actor) Actor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	return Actor;
}

UMaterialInterface* ATrackGenerator::EntryMaterial(int32 EntryIndex, const FLinearColor& Color)
{
	if (!BaseFeatureMaterial) return nullptr;
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = EntryMaterials.Find(EntryIndex)) return *Found;
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseFeatureMaterial, this);
	Mid->SetVectorParameterValue(BaseColorParam, Color);
	Mid->SetVectorParameterValue(EmissionColorParam, Color);
	EntryMaterials.Add(EntryIndex, Mid);
	return Mid;
}

AJumpRamp* ATrackGenerator::BuildRamp(float Distance, float Lateral, float HalfWidth, const UJumpDefinition& Def, UMaterialInterface* Material, float Boost, bool bEndRamp)
{
	AJumpRamp* Ramp = SpawnOnTrack<AJumpRamp>(Distance, Lateral);
	if (!Ramp) return nullptr;
	Ramp->Configure(&Def, Distance, Lateral, HalfWidth, Boost, bEndRamp);
	Ramp->BuildPicture(Box, Material);
	Ramps.Add(Ramp);
	return Ramp;
}

void ATrackGenerator::BuildJump(const FTrackPlacement& Placement)
{
	const FTrackFeatureEntry* Entry = Decider->FeatureEntry(Placement.Variant);
	const UJumpDefinition* Def = Entry ? Cast<UJumpDefinition>(Entry->Definition) : nullptr;
	if (!Def) return;
	const float HalfWidth = GetTrack()->GetHalfWidth() * FMath::Clamp(Def->WidthFraction, 0.f, 1.f);
	const float Boost = Decider->GetContext().GetBaseBoost() * Entry->Multiplier;
	AJumpRamp* Ramp = BuildRamp(Placement.Distance, Placement.Lateral, HalfWidth, *Def, EntryMaterial(Placement.Variant, Entry->Color), Boost, false);
	Remember(Ramp, Placement.Distance, Placement);
}

void ATrackGenerator::BuildEndRamp(const FTrackPlacement& Placement)
{
	const UJumpDefinition* Def = Decider->GetEndRamp();
	if (!Def) return;
	float Width, SideGap, Half;
	const float Gap = Decider->EndRampLayout(Width, SideGap);
	Decider->EndRampLateral(Placement.Variant - 1, Width, Gap, Half);
	AJumpRamp* Ramp = BuildRamp(Placement.Distance, Placement.Lateral, Half, *Def, EntryMaterial(-1, FLinearColor(1.f, 0.8f, 0.2f)), 0.f, true);
	// An end ramp is keyed on its END: it must outlive the whole run-up.
	Remember(Ramp, Placement.Distance + Def->Length, Placement);

	// The middle one marks the two gaps as drops.
	if (Placement.Variant == 1)
	{
		for (const float Side : { -1.f, 1.f })
		{
			Decorator->StampEndMarker(Placement.Distance, Side * (Width + Gap) * 0.5f, Gap, Def->Length);
		}
	}
}

void ATrackGenerator::BuildLoop(const FTrackPlacement& Placement)
{
	const FTrackFeatureEntry* Entry = Decider->FeatureEntry(Placement.Variant);
	const ULoopDefinition* Def = Entry ? Cast<ULoopDefinition>(Entry->Definition) : nullptr;
	const FTrackSection* Section = GetTrack()->SectionAt(Placement.Distance);
	const FLoopSection* Loop = Section ? Section->AsLoop() : nullptr;
	if (!Def || !Loop) return;

	ALoopGate* Gate = SpawnOnTrack<ALoopGate>(Placement.Distance, 0.f);
	if (!Gate) return;
	const float Required = Rules ? Rules->GetLoopRequiredSpeed(Placement.Distance) : 0.f;
	Gate->Configure(Def, Loop, Required);
	Gate->BuildPicture(Box, EntryMaterial(Placement.Variant, Entry->Color), GetTrack()->GetHalfWidth(),
		Rules ? Rules->GetSpeedDisplayMultiplier() : 3.6f);
	Loops.Add(Gate);
	// Culled by its EXIT: a loop is 2πR of track and the cull line trails the ship by far less.
	Remember(Gate, Loop->GetEndDistance(), Placement);
}

AJumpRamp* ATrackGenerator::RampAt(float Distance) const
{
	for (const TObjectPtr<AJumpRamp>& Ramp : Ramps)
	{
		if (IsValid(Ramp) && Ramp->Spans(Distance)) return Ramp;
	}
	return nullptr;
}
