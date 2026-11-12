#include "Track/Features/LaserGate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Prefabs/RunnerPrefabLayout.h"
#include "Simulation/PickupRegistry.h"
#include "Track/RunnerColor.h"

namespace
{
	/** The stand-in emitters' barrel radius at scale 1 (the authored emitters are ~1.2 m across). */
	constexpr float StandInEmitterRadius = 0.6f;
	/** The authored laser model's name in PF_LaserSystem (Unity's LaserBeam finds it the same way). */
	const TCHAR* const PrefabModelName = TEXT("laserTypeA");
}

ALaserGate::ALaserGate()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void ALaserGate::Configure(const ULaserGateDefinition& InDefinition, ELaserGateVariant InVariant, float Distance, float Lateral, float Length,
	float RotorDegreesPerSecond, float RotorPhaseDegrees, float RoadSurface, bool bInWavy, const URunnerPrefabLayout* Layout,
	UMaterialInterface* RodMaterial)
{
	Definition = &InDefinition;
	Variant = InVariant;
	WaveAmplitude = bInWavy ? InDefinition.WaveAmplitude : 0.f;
	BurnRadius = InDefinition.BeamRadius;
	TrackDistance = Distance;
	Beams.Reset();
	bRotor = false;

	const float Half = Length * 0.5f;
	const float H = InDefinition.BeamHeight;
	switch (Variant)
	{
	case ELaserGateVariant::Vertical:
	{
		// The authored model is never stretched: the beam is as tall as its sheet and burns as wide as it is.
		const bool bModel = InDefinition.DrawsPrefab() && Layout && BuildPrefabPicture(*Layout, RoadSurface, Lateral);
		const float Top = bModel ? RoadSurface + SheetLength : InDefinition.VerticalHeight;
		if (bModel) BurnRadius = SheetHalfWidth;
		Beams.Add({ FVector(0.f, Lateral, RoadSurface), FVector(0.f, Lateral, Top) });
		break;
	}
	case ELaserGateVariant::Triple:
		for (int32 i = 0; i < 3; ++i)
		{
			const float Z = H + i * InDefinition.TripleSpacing;
			Beams.Add({ FVector(0.f, Lateral - Half, Z), FVector(0.f, Lateral + Half, Z) });
		}
		break;
	case ELaserGateVariant::Rotor:
		bRotor = true;
		RotorCentre = FVector(0.f, Lateral, H);
		RotorHalfLength = Half;
		RotorSpeed = RotorDegreesPerSecond;
		RotorPhase = RotorPhaseDegrees;
		Beams.Add(FBeam()); // posed from the angle
		break;
	default:
		Beams.Add({ FVector(0.f, Lateral - Half, H), FVector(0.f, Lateral + Half, H) });
		break;
	}

	VisualLift = Variant == ELaserGateVariant::Vertical ? 0.f : LiftClearOfRoad(RoadSurface, StandInEmitterRadius * InDefinition.EmitterScale);
	ComputeBounds();

	if (!bPrefabDrawn)
	{
		BuildRods(RodMaterial);
		const UWorld* World = GetWorld();
		PoseRods(World ? World->GetTimeSeconds() : 0.f);
	}
	SetActorTickEnabled(bRotor && Rods.Num() > 0);

	bPlaced = true;
	if (UPickupRegistry* Registry = UPickupRegistry::Get(this)) Registry->Register(this);
}

float ALaserGate::GetEmitterReach() const
{
	return Definition ? Definition->EmitterScale : 0.f;
}

void ALaserGate::GetSegment(int32 Index, float Time, FVector& OutA, FVector& OutB) const
{
	if (!bRotor)
	{
		OutA = Beams[Index].A;
		OutB = Beams[Index].B;
		return;
	}
	// One clock for the picture and the test; Unity's arm (cos, 0, sin) is (across, up, along).
	const float Angle = FMath::DegreesToRadians(RotorPhase + RotorSpeed * Time);
	const FVector Arm = FVector(FMath::Sin(Angle), FMath::Cos(Angle), 0.f) * RotorHalfLength;
	OutA = RotorCentre - Arm;
	OutB = RotorCentre + Arm;
}

float ALaserGate::LiftClearOfRoad(float RoadSurface, float EmitterRadius) const
{
	// Unity counts the rotor's placeholder beam (at the origin) too.
	float Lowest = bRotor ? RotorCentre.Z : TNumericLimits<float>::Max();
	for (const FBeam& Beam : Beams) Lowest = FMath::Min(Lowest, FMath::Min(Beam.A.Z, Beam.B.Z));
	if (Lowest == TNumericLimits<float>::Max()) return 0.f;
	return FMath::Max(0.f, RoadSurface + Definition->EmitterRoadClearance + EmitterRadius - Lowest);
}

void ALaserGate::ComputeBounds()
{
	const float R = BurnRadius;
	if (bRotor)
	{
		BoundsCentre = RotorCentre;
		BoundsHalf = FVector(RotorHalfLength + R, RotorHalfLength + R, R + WaveAmplitude);
		return;
	}
	FBox Box(ForceInit);
	for (const FBeam& Beam : Beams)
	{
		Box += Beam.A;
		Box += Beam.B;
	}
	// A wavy beam swings along the track's up, or across the track when it stands up itself.
	const FVector WaveReach = Variant == ELaserGateVariant::Vertical ? FVector(0.f, WaveAmplitude, 0.f) : FVector(0.f, 0.f, WaveAmplitude);
	BoundsCentre = Box.GetCenter();
	BoundsHalf = Box.GetExtent() + FVector(R) + WaveReach;
}

bool ALaserGate::BuildPrefabPicture(const URunnerPrefabLayout& Layout, float RoadSurface, float Lateral)
{
	const FRunnerPrefabNode* Model = Layout.FindNamed(PrefabModelName);
	if (!Model) return false;
	const FRunnerPrefabNode* Sheet = Layout.FindNamed(TEXT("Plane"), Model->Path);
	const FRunnerPrefabNode* Shoot = Layout.FindNamed(TEXT("ShootPoint"), Model->Path);
	if (!Sheet || !Shoot) return false;

	// The model at its authored size times the definition's scale (and the prefab root's own, uniform).
	const double K = Layout.RootScale.X * Definition->PrefabModelScale;
	const FVector ShootPos = Shoot->Position * 100.0;
	const FQuat ShootRot = Shoot->Rotation;
	const FVector Forward = ShootRot.GetAxisX(); // Unity's forward: down the sheet
	const FVector Right = ShootRot.GetAxisY();   // Unity's right: across it

	// How far the sheet reaches down the shoot point's forward, and how wide it is across its right.
	TArray<FVector> Corners;
	RunnerPrefab::NodeCorners(*Sheet, Corners);
	double Length = 0.0, MinX = TNumericLimits<double>::Max(), MaxX = -TNumericLimits<double>::Max();
	for (const FVector& C : Corners)
	{
		const FVector V = C - ShootPos;
		Length = FMath::Max(Length, FVector::DotProduct(V, Forward));
		const double X = FVector::DotProduct(V, Right);
		MinX = FMath::Min(MinX, X);
		MaxX = FMath::Max(MaxX, X);
	}
	const double Width = MaxX > MinX ? MaxX - MinX : 0.0;
	SheetLength = static_cast<float>(Length * K / 100.0);
	SheetHalfWidth = static_cast<float>(Width * K / 100.0 * 0.5);
	if (SheetLength < 1e-3f || SheetHalfWidth < 1e-3f) return false;

	// Muzzle B (the top) fires at A: the shoot point looks down the beam with its up along the track,
	// so the sheet spans across the road and faces along it.
	const FVector Muzzle = FVector(0.0, Lateral, RoadSurface + SheetLength) * 100.0;
	const FQuat Target = FRotationMatrix::MakeFromXZ(-FVector::UpVector, FVector::ForwardVector).ToQuat();
	const FQuat ModelRot = Target * ShootRot.Inverse();
	const FVector ModelPos = Muzzle - ModelRot.RotateVector(ShootPos * K);

	USceneComponent* ModelRoot = NewObject<USceneComponent>(this, TEXT("LaserModel"), RF_Transient);
	ModelRoot->SetMobility(EComponentMobility::Movable);
	ModelRoot->SetupAttachment(Root);
	ModelRoot->SetRelativeTransform(FTransform(ModelRot, ModelPos, FVector(K)));
	AddInstanceComponent(ModelRoot);
	ModelRoot->RegisterComponent();

	const FString Under = Model->Path + TEXT("/");
	for (const FRunnerPrefabNode& Node : Layout.Nodes)
	{
		if (!Node.IsDrawn() || (Node.Path != Model->Path && !Node.Path.StartsWith(Under))) continue;
		RunnerPrefab::AddNodeMesh(Node, *ModelRoot, Node.GetTransform());
	}
	// The sheet draws front faces only: a copy turned half round the beam shows its back.
	const FTransform Turn = FTransform(-ShootPos) * FTransform(FQuat(Forward, UE_PI)) * FTransform(ShootPos);
	RunnerPrefab::AddNodeMesh(*Sheet, *ModelRoot, Sheet->GetTransform() * Turn);

	bPrefabDrawn = true;
	return true;
}

void ALaserGate::BuildRods(UMaterialInterface* Material)
{
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!Cylinder) return;
	UMaterialInstanceDynamic* Mid = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (Mid)
	{
		const FLinearColor Color = RunnerColor::FromAuthored(Definition->BeamColor);
		Mid->SetVectorParameterValue(RunnerColor::BaseColorParam, Color);
		Mid->SetVectorParameterValue(RunnerColor::EmissionColorParam, Color * 4.f);
	}
	for (int32 i = 0; i < Beams.Num(); ++i)
	{
		UStaticMeshComponent* Rod = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Rod->SetStaticMesh(Cylinder);
		Rod->SetCollisionEnabled(ECollisionEnabled::NoCollision); // beams stay analytic
		Rod->SetMobility(EComponentMobility::Movable);
		Rod->SetCastShadow(false);
		Rod->SetupAttachment(Root);
		if (Mid) Rod->SetMaterial(0, Mid);
		AddInstanceComponent(Rod);
		Rod->RegisterComponent();
		Rods.Add(Rod);
	}
}

void ALaserGate::PoseRods(float Time)
{
	for (int32 i = 0; i < Rods.Num() && i < Beams.Num(); ++i)
	{
		FVector A, B;
		GetSegment(i, Time, A, B);
		A.Z += VisualLift;
		B.Z += VisualLift;
		const FVector Along = B - A;
		const double Len = Along.Size();
		if (Len < 1e-3) continue;
		const FQuat Rot = FRotationMatrix::MakeFromZ(Along / Len).ToQuat();
		const double Thick = BurnRadius * 2.0; // the engine cylinder is 1 m across and 1 m tall
		Rods[i]->SetRelativeTransform(FTransform(Rot, (A + B) * 0.5 * 100.0, FVector(Thick, Thick, Len)));
	}
}

void ALaserGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UWorld* World = GetWorld()) PoseRods(World->GetTimeSeconds());
}

void ALaserGate::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UPickupRegistry* Registry = UPickupRegistry::Get(this)) Registry->Unregister(this);
	Super::EndPlay(Reason);
}
