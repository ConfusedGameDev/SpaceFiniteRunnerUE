#include "Track/Spawning/TrackSpawner.h"

#include "TrackContracts.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Track/Features/LaserGate.h"
#include "Track/Pickups/RepairOrb.h"
#include "Track/Pickups/SpeedOrb.h"
#include "Track/RunnerColor.h"
#include "Track/Spawning/PadDefinition.h"
#include "Track/Spawning/TrackSpawnContext.h"
#include "Track/Spawning/WeightedTable.h"
#include "Track/TrackModel.h"

namespace
{
	const TCHAR* const LitMaterialPath = TEXT("/Game/Runner/Materials/Lit/M_Lit.M_Lit");
	const TCHAR* const LitTranslucentMaterialPath = TEXT("/Game/Runner/Materials/Lit/M_LitTranslucent.M_LitTranslucent");

	/** A tinted copy of a lit master: base colour and emission (times Glow) from an authored sRGB colour. */
	UMaterialInstanceDynamic* Tinted(UObject* Outer, const TCHAR* Path, const FLinearColor& Authored, float Glow = 1.f)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!Base) return nullptr;
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Outer);
		const FLinearColor Linear = RunnerColor::FromAuthored(Authored);
		Mid->SetVectorParameterValue(RunnerColor::BaseColorParam, Linear);
		Mid->SetVectorParameterValue(RunnerColor::EmissionColorParam, FLinearColor(Linear.R * Glow, Linear.G * Glow, Linear.B * Glow, 1.f));
		return Mid;
	}
}

// ------------------------------------------------------------------ base

void UTrackSpawner::Begin(const FTrackSpawnContext& Ctx)
{
	Rng = FRunnerRandom(Hash2(Ctx.LayoutSeed, NameHash(DisplayName)) | 1u);
	Cursor = StartDistance + Rng.NextFloat(0.f, Spacing.Min);
	OnBegin(Ctx);
}

void UTrackSpawner::PlaceUpTo(FTrackSpawnContext& Ctx, float Limit)
{
	if (!IsActive(Ctx))
	{
		Cursor = FMath::Max(Cursor, Limit);
		return;
	}

	while (Cursor < Limit)
	{
		if (Chance < 1.f && Rng.NextFloat() >= Chance)
		{
			Advance();
			continue;
		}

		const float Resume = Step(Ctx, Cursor, Limit);
		if (Resume == TNumericLimits<float>::Max() || (!FMath::IsFinite(Resume) && Resume > 0.f))
		{
			// Nothing ever fits again (the end zone).
			Cursor = TNumericLimits<float>::Max();
			return;
		}
		if (Resume >= 0.f)
		{
			Cursor = FMath::Max(Resume, Cursor + 1.f);
			continue;
		}
		Advance();
	}
}

float UTrackSpawner::GetAmountPerTenKm() const
{
	return bActive ? 10000.f * FMath::Clamp(Chance, 0.f, 1.f) / FMath::Max(1.f, (Spacing.Min + Spacing.Max) * 0.5f) : 0.f;
}

uint32 UTrackSpawner::NameHash(const FString& Name)
{
	// FNV-1a over the UTF-16 code units, as C# iterates a string's chars.
	uint32 Hash = 2166136261u;
	for (const TCHAR C : Name)
	{
		Hash = (Hash ^ static_cast<uint32>(static_cast<uint16>(C))) * 16777619u;
	}
	return Hash;
}

uint32 UTrackSpawner::Hash2(uint32 A, uint32 B)
{
	return A * 0x4473BBB1u + B * 0xCBA11D5Fu + 0x685835CFu;
}

// ------------------------------------------------------------ speed orbs

float USpeedOrbSpawner::Step(FTrackSpawnContext& Ctx, float Distance, float Limit)
{
	const float ClaimEnd = Ctx.ClaimEnd(Distance);
	if (ClaimEnd >= 0.f)
	{
		return ClaimEnd;
	}

	const int32 TierIndex = FWeightedTable::Pick(Tiers, [](const FPadSpawnEntry& E) { return E.Probability; }, Rng.NextFloat());
	if (TierIndex == INDEX_NONE || Tiers[TierIndex].Definition == nullptr)
	{
		return -1.f;
	}
	const FPadSpawnEntry& Tier = Tiers[TierIndex];
	const float Lateral = Ctx.RandomLateral(Rng, Distance, Ctx.PadMargin(Tier.Definition, Tier.SwayAmplitude));
	Ctx.RecordPickup(Distance);

	FTrackPlacement Placement;
	Placement.Kind = GetKind();
	Placement.Distance = Distance;
	Placement.Lateral = Lateral;
	Placement.Variant = TierIndex;
	Ctx.Emit(Placement);
	return -1.f;
}

AActor* USpeedOrbSpawner::Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement)
{
	if (!Tiers.IsValidIndex(Placement.Variant) || Tiers[Placement.Variant].Definition == nullptr) return nullptr;
	const FPadSpawnEntry& Tier = Tiers[Placement.Variant];
	const UPadDefinition& Def = *Tier.Definition;

	// Orbs sit on the flight line, flat pads sink to the road; the air lane rides the track's up.
	const float AirHeight = Tier.Lane == EPadLane::Air ? Ctx.GetAirLaneHeight() : 0.f;
	const float Up = (Def.bFloatingOrb ? 0.f : -0.9f) + AirHeight;
	ASpeedOrb* Orb = Ctx.SpawnOnTrack<ASpeedOrb>(Placement.Distance, Placement.Lateral, Up);
	if (!Orb) return nullptr;

	// The pickup volume in track space (along, across, up): an orb is a ball of its own size, a flat
	// pad the whole slab and as tall as the ship, so a grounded ship always reads as on it.
	const float S = Def.SizeMultiplier;
	FVector3f Half = Def.bFloatingOrb
		? FVector3f(Ctx.PadSize.X * S * 0.5f)
		: FVector3f(Ctx.PadSize.Z * S * 0.5f, Ctx.PadSize.X * S * 0.5f, 1.f);
	Half *= Tier.PickupScale;

	// Boosts scale off the shared power-up base; brakes keep their own delta so dodging stays predictable.
	const float Delta = Def.SpeedDelta >= 0.f ? Ctx.GetBaseBoost() * Tier.Multiplier : Def.SpeedDelta;
	Orb->Configure(Tier, Placement.Variant, Delta, Placement.Distance, Placement.Lateral, AirHeight, Half, Ctx.PadSize,
		Tier.Prefab ? nullptr : TierMaterial(Placement.Variant));
	return Orb;
}

UMaterialInterface* USpeedOrbSpawner::TierMaterial(int32 TierIndex)
{
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = TierMaterials.Find(TierIndex)) return *Found;
	UMaterialInstanceDynamic* Mid = Tinted(this, LitMaterialPath, Tiers[TierIndex].Color);
	TierMaterials.Add(TierIndex, Mid);
	return Mid;
}

// ----------------------------------------------------------- repair orbs

bool URepairOrbSpawner::IsActive(const FTrackSpawnContext& Ctx) const
{
	return Super::IsActive(Ctx) && (Ctx.Rules == nullptr || Ctx.Rules->IsHullEnabled());
}

float URepairOrbSpawner::Step(FTrackSpawnContext& Ctx, float Distance, float Limit)
{
	const float ClaimEnd = Ctx.ClaimEnd(Distance);
	if (ClaimEnd >= 0.f)
	{
		return ClaimEnd;
	}
	if (Ctx.NearPickup(Distance))
	{
		return -1.f;
	}

	const float Lateral = Ctx.RandomLateral(Rng, Distance, GetDiameter(Ctx.PadSize) * 0.5f + 2.f);
	Ctx.RecordPickup(Distance); // coins keep off it too

	FTrackPlacement Placement;
	Placement.Kind = GetKind();
	Placement.Distance = Distance;
	Placement.Lateral = Lateral;
	Ctx.Emit(Placement);
	return -1.f;
}

AActor* URepairOrbSpawner::Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement)
{
	const float Diameter = GetDiameter(Ctx.PadSize);
	// Lifted along the track's up (so it holds on a bank or in a tube) until its lowest point, the
	// radius and the bob below the centre, clears the visible road.
	const float Bob = GetDefault<ARepairOrb>()->GetBobAmplitude();
	const float Lift = Ctx.RoadSurfaceOffset + RoadClearance + Bob + Diameter * 0.5f;
	ARepairOrb* Orb = Ctx.SpawnOnTrack<ARepairOrb>(Placement.Distance, Placement.Lateral, Lift);
	if (!Orb) return nullptr;

	if (!Prefab && !ShellMaterial)
	{
		ShellMaterial = Tinted(this, LitTranslucentMaterialPath, ShellColor, 0.6f);
		CrossMaterial = Tinted(this, LitMaterialPath, CrossColor, 2.f);
	}
	Orb->Configure(Placement.Distance, Diameter, SpinDegreesPerSecond, Prefab, ShellMaterial, CrossMaterial);
	return Orb;
}

// ----------------------------------------------------------- laser gates

bool ULaserGateSpawner::IsActive(const FTrackSpawnContext& Ctx) const
{
	return Super::IsActive(Ctx) && Runtime != nullptr && Runtime->GetTotalWeight() > 0.f;
}

void ULaserGateSpawner::OnBegin(const FTrackSpawnContext& Ctx)
{
	// Gameplay never writes a DataAsset: a run plays a copy.
	Runtime = Definition != nullptr && Ctx.World.IsValid() && Ctx.World->IsGameWorld()
		? DuplicateObject<ULaserGateDefinition>(Definition, this)
		: Definition.Get();
}

ELaserGateVariant ULaserGateSpawner::ResolveVariant(ELaserGateVariant Rolled) const
{
	return Runtime != nullptr && Runtime->DrawsPrefab() ? ELaserGateVariant::Vertical : Rolled;
}

float ULaserGateSpawner::HalfDepth(ELaserGateVariant Variant, float Length) const
{
	return Runtime->BeamRadius + (Variant == ELaserGateVariant::Rotor ? Length * 0.5f : 0.f);
}

float ULaserGateSpawner::Step(FTrackSpawnContext& Ctx, float Distance, float Limit)
{
	// Rolled either way, so seeds keep their rng order.
	const ELaserGateVariant Variant = ResolveVariant(Runtime->PickVariant(Rng.NextFloat()));
	float BandMin, BandMax;
	Ctx.Track->GetLateralBand(Distance, BandMin, BandMax);
	const float Length = (BandMax - BandMin) * Rng.NextFloat(Runtime->GetCoverageMin(), Runtime->GetCoverageMax());
	const float Depth = HalfDepth(Variant, Length);
	const float BlockedUntil = Ctx.KeepOutUntil(Distance, Depth + Clearance, bOnFlatSweeps, bOnOpenEdges);
	if (BlockedUntil >= 0.f)
	{
		return FMath::IsFinite(BlockedUntil) ? FMath::Max(BlockedUntil, Distance + 10.f) : BlockedUntil;
	}

	// The rest of the gate is rolled here too, in Unity's order: the beam's lateral (so the whole
	// beam and the emitters' bulk stay in the lane), the rotor, the wave.
	const float HalfAcross = Variant == ELaserGateVariant::Vertical ? Runtime->BeamRadius : Length * 0.5f;
	const float Margin = HalfAcross + 2.f * Runtime->EmitterScale; // the emitter is ~2 m long at scale 1
	const float Lo = BandMin + Margin;
	const float Hi = BandMax - Margin;
	const float Lateral = Hi > Lo ? Rng.NextFloat(Lo, Hi) : (BandMin + BandMax) * 0.5f;
	const float RotorSpeed = Rng.NextFloat(Runtime->GetRotorSpeedMin(), Runtime->GetRotorSpeedMax()) * (Rng.NextBool() ? 1.f : -1.f);
	const float RotorPhase = Rng.NextFloat(0.f, 360.f);
	bool bWavy = Runtime->bWavy && Rng.NextFloat() < Runtime->WaveChance; // no draw while the wave is off
	bWavy &= Runtime->DrawsProcedural(); // the authored sheet cannot zigzag: a wave nobody sees must not burn

	Ctx.Claim(Distance - Depth, Distance + Depth);
	FTrackPlacement Placement;
	Placement.Kind = GetKind();
	Placement.Distance = Distance;
	Placement.Lateral = Lateral;
	Placement.Variant = static_cast<int32>(Variant);
	Placement.Data = FVector4(Length, RotorSpeed, RotorPhase, bWavy ? 1.f : 0.f);
	Ctx.Emit(Placement);
	return -1.f;
}

AActor* ULaserGateSpawner::Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement)
{
	if (!Runtime) return nullptr;
	const ELaserGateVariant Variant = ResolveVariant(static_cast<ELaserGateVariant>(Placement.Variant));
	const float Length = static_cast<float>(Placement.Data.X);
	const bool bWavy = Placement.Data.W > 0.5 && Runtime->DrawsProcedural();

	// The root on the track pose at the flight line's middle; the gate places its beams across it.
	ALaserGate* Gate = Ctx.SpawnOnTrack<ALaserGate>(Placement.Distance, 0.f);
	if (!Gate) return nullptr;
	UMaterialInterface* RodMaterial = Runtime->BeamMaterial ? Runtime->BeamMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, LitMaterialPath);
	Gate->Configure(*Runtime, Variant, Placement.Distance, Placement.Lateral, Length, static_cast<float>(Placement.Data.Y),
		static_cast<float>(Placement.Data.Z), Ctx.RoadSurfaceOffset, bWavy, LaserPrefab, RodMaterial);
	return Gate;
}

float ULaserGateSpawner::GetCullDistance(const FTrackPlacement& Placement) const
{
	if (!Runtime) return Placement.Distance;
	const ELaserGateVariant Variant = ResolveVariant(static_cast<ELaserGateVariant>(Placement.Variant));
	return Placement.Distance + HalfDepth(Variant, static_cast<float>(Placement.Data.X));
}
