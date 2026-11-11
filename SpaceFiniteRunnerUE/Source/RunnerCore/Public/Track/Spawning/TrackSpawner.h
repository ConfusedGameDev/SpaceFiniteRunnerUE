#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Track/Features/LaserGateDefinition.h"
#include "Track/RunnerRandom.h"
#include "Track/TrackLayout.h"
#include "TrackSpawner.generated.h"

class AActor;
class FTrackSpawnContext;
class UPadDefinition;

/** When a spawner runs in a decide pass: ground claimers first, then pickups in set order. */
enum class ETrackSpawnPhase : uint8
{
	ClaimsGround,
	Pickup,
};

/** Which lane a pad spawns in. Serialized: append only, values match Unity. */
UENUM(BlueprintType)
enum class EPadLane : uint8
{
	/** On the flight line. */
	Ground = 0,
	/** The air-lane height above it, where only a jump reaches. */
	Air = 1,
};

/** One tier of pickup: its look, what it does, how often it wins a roll, its boost, colour and motion. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FPadSpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	FString Name = TEXT("Pad");

	/** Optional actor spawned instead of the built-in orb (the Unity prefab, rebuilt). Empty = the default orb actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	TSoftClassPtr<AActor> ActorClass;

	/** What the pad does on pickup (boost/brake, orb or flat pad, size). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	TObjectPtr<UPadDefinition> Definition;

	/** Share of every spawn roll this entry wins. The tiers always sum to 100 %. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0, ClampMax = 100))
	float Probability = 100.f;

	/** Boosts only: multiplies the game settings' power-up speed boost. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0))
	float Multiplier = 1.f;

	/** Tint of the orb and of the pickup's story colour (sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	FLinearColor Color = FLinearColor::Green;

	/** How far the orb sways side to side across the track. 0 = holds the flight line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0, Units = "m"))
	float SwayAmplitude = 0.f;

	/** Sway cycles per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0, Units = "Hz"))
	float SwayFrequency = 0.f;

	/** Floating orbs only: turn rate about the track's up, degrees per second. 0 = holds its authored rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0, ClampMax = 360))
	float SpinDegreesPerSecond = 60.f;

	/** Scales the pickup volume without touching the picture. 2 = twice as easy to hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad", meta = (ClampMin = 0.5, ClampMax = 4))
	float PickupScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pad")
	EPadLane Lane = EPadLane::Ground;
};

/**
 * Something the generator streams onto the track besides features: decided in Step (every roll,
 * the claim, the placement record), built later inside the stream window by Build. Each spawner
 * draws from its own random stream (hash of the layout seed and its name), so one spawner never
 * moves another's layout and the road never depends on what spawns. The generator plays a runtime
 * copy of every set entry.
 */
UCLASS(Abstract, BlueprintType)
class RUNNERCORE_API UTrackSpawner : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Off = listed but never spawned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	bool bActive = true;

	/** Name shown in the debug menu and used to seed this spawner's random stream (renaming reshuffles its layout). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	FString DisplayName = TEXT("Spawner");

	/** Tint of this spawner's debug rows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	FLinearColor Color = FLinearColor::White;

	/** Metres of track between one spawn step and the next (min, max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = 10, ClampMax = 20000, Units = "m"))
	FFloatInterval Spacing = FFloatInterval(400.f, 700.f);

	/** The first step lands between this distance and this plus the minimum spacing, so the launch stays clean. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = 0, ClampMax = 5000, Units = "m"))
	float StartDistance = 120.f;

	/** Chance that a step spawns anything at all. 1 = every step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = 0, ClampMax = 1))
	float Chance = 1.f;

	virtual ETrackSpawnPhase GetPhase() const { return ETrackSpawnPhase::Pickup; }
	virtual bool IsActive(const FTrackSpawnContext& Ctx) const { return bActive; }
	/** The record kind this spawner emits: how the generator routes a record back to it. */
	virtual ETrackPlacementKind GetKind() const { return ETrackPlacementKind::SpeedOrb; }

	/** Seeds this spawner's stream off the layout seed and puts the cursor at its first step. */
	void Begin(const FTrackSpawnContext& Ctx);
	/** Decides every step up to Limit. */
	void PlaceUpTo(FTrackSpawnContext& Ctx, float Limit);
	/** Nothing more is decided (a saved track). */
	void Park() { Cursor = TNumericLimits<float>::Max(); }
	float GetCursor() const { return Cursor; }

	/** Builds one decided record inside the stream window. Returns the actor (or null). */
	virtual AActor* Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement) { return nullptr; }

	/** Spawns per 10 km at the current spacing and chance. */
	float GetAmountPerTenKm() const;

	/** FNV-1a of a name, stable across runs and platforms. */
	static uint32 NameHash(const FString& Name);
	/** Unity.Mathematics math.hash(uint2). */
	static uint32 Hash2(uint32 A, uint32 B);

protected:
	virtual void OnBegin(const FTrackSpawnContext& Ctx) {}
	/** One step at Distance: -1 = done (move on by the spacing), a distance to retry from, or +inf = never again. */
	virtual float Step(FTrackSpawnContext& Ctx, float Distance, float Limit) { return -1.f; }

	FRunnerRandom Rng;

private:
	void Advance() { Cursor += Rng.NextFloat(Spacing.Min, Spacing.Max); }

	float Cursor = 0.f;
};

/** The speed orbs: one weighted draw over the tiers per step. */
UCLASS(BlueprintType, meta = (UnityType = "SpeedOrbSpawner"))
class RUNNERCORE_API USpeedOrbSpawner : public UTrackSpawner
{
	GENERATED_BODY()

public:
	/** One entry per orb tier. Every step draws one by probability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed Orbs")
	TArray<FPadSpawnEntry> Tiers;

	virtual ETrackPlacementKind GetKind() const override { return ETrackPlacementKind::SpeedOrb; }
	virtual AActor* Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement) override;

protected:
	virtual float Step(FTrackSpawnContext& Ctx, float Distance, float Limit) override;
};

/** Repair orbs: a translucent shell with a cross, healing the hull. Never a speed pad. */
UCLASS(BlueprintType, meta = (UnityType = "RepairOrbSpawner"))
class RUNNERCORE_API URepairOrbSpawner : public UTrackSpawner
{
	GENERATED_BODY()

public:
	/** The repair orb's look, modelled at 1 m across. Empty = the default repair orb actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb")
	TSoftClassPtr<AActor> ActorClass;

	/** Diameter of the repair orb as a share of the pad width (a boost orb's is its definition's size multiplier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb", meta = (ClampMin = 0.1, ClampMax = 2))
	float Size = 1.f;

	/** Gap between the visible road and the orb's lowest point (the bottom of its bob), along the track's up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb", meta = (ClampMin = 0, ClampMax = 10, Units = "m"))
	float RoadClearance = 1.5f;

	/** Spin about the track's up, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb", meta = (ClampMin = 0, ClampMax = 720))
	float SpinDegreesPerSecond = 60.f;

	/** Colour of the shell (alpha = how see-through; sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb")
	FLinearColor ShellColor = FLinearColor(1.f, 0.1f, 0.1f, 0.45f);

	/** Colour of the glowing cross (sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Repair Orb")
	FLinearColor CrossColor = FLinearColor(0.1f, 1.f, 0.25f);

	float GetDiameter(const FVector3f& PadSize) const { return PadSize.X * Size; }

	/** Off when the run has no hull. */
	virtual bool IsActive(const FTrackSpawnContext& Ctx) const override;
	virtual ETrackPlacementKind GetKind() const override { return ETrackPlacementKind::RepairOrb; }
	virtual AActor* Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement) override;

protected:
	virtual float Step(FTrackSpawnContext& Ctx, float Distance, float Limit) override;
};

/** Laser gates: claim their ground before every pickup in a pass. */
UCLASS(BlueprintType, meta = (UnityType = "LaserGateSpawner"))
class RUNNERCORE_API ULaserGateSpawner : public UTrackSpawner
{
	GENERATED_BODY()

public:
	/** The gate actor (the Unity PF_LaserSystem, rebuilt). Empty = the default gate actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser Gates")
	TSoftClassPtr<AActor> ActorClass;

	/** Beam size, variant weights, rotor speed and the look. Played as a runtime copy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser Gates")
	TObjectPtr<ULaserGateDefinition> Definition;

	/** Clear road kept between a gate and every feature, either side. Must stay shorter than the generator's settle margin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser Gates", meta = (ClampMin = 0, ClampMax = 500, Units = "m"))
	float Clearance = 150.f;

	/** Gates may stand on a flat sweep (no bank, the outer wall gone). Off = the clearance is kept round every flat sweep too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser Gates")
	bool bOnFlatSweeps = true;

	/** Gates may stand where the road has no wall (an open straight, the outer edge of a flat sweep). Off = walled road only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser Gates")
	bool bOnOpenEdges = true;

	virtual ETrackSpawnPhase GetPhase() const override { return ETrackSpawnPhase::ClaimsGround; }
	/** Needs a definition with some variant weight. */
	virtual bool IsActive(const FTrackSpawnContext& Ctx) const override;
	virtual ETrackPlacementKind GetKind() const override { return ETrackPlacementKind::LaserGate; }
	virtual AActor* Build(FTrackSpawnContext& Ctx, const FTrackPlacement& Placement) override;

	/** The definition this pass plays (a copy at runtime, the asset in the editor). */
	const ULaserGateDefinition* GetRuntimeDefinition() const { return Runtime; }
	/** The variant a record draws as: the authored model only stands up, so a look that draws it makes every gate vertical. */
	ELaserGateVariant ResolveVariant(ELaserGateVariant Rolled) const;
	/** Half the stretch of road a gate holds: only the rotor has depth (its blade sweeps a disc). */
	float HalfDepth(ELaserGateVariant Variant, float Length) const;

protected:
	virtual void OnBegin(const FTrackSpawnContext& Ctx) override;
	virtual float Step(FTrackSpawnContext& Ctx, float Distance, float Limit) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ULaserGateDefinition> Runtime;
};

/** Everything the generator spawns along the track. Add a spawner asset to spawn it, remove it to stop. */
UCLASS(BlueprintType, meta = (UnityType = "TrackSpawnSet"))
class RUNNERCORE_API UTrackSpawnSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Run in this order within each phase (repair orbs after speed orbs, so they keep off them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Set")
	TArray<TObjectPtr<UTrackSpawner>> Spawners;
};
