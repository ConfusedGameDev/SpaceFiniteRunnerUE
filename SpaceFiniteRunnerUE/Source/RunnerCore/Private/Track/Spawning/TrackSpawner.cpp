#include "Track/Spawning/TrackSpawner.h"

#include "TrackContracts.h"
#include "Track/Spawning/PadDefinition.h"
#include "Track/Spawning/TrackSpawnContext.h"
#include "Track/Spawning/WeightedTable.h"
#include "Track/TrackModel.h"

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
	// The orb actors come in M2.5.
	return nullptr;
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
	return nullptr;
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
	return nullptr;
}
