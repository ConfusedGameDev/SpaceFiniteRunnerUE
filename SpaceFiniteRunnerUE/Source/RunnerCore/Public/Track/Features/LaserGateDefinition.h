#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LaserGateDefinition.generated.h"

class UMaterialInterface;

/** What draws a laser gate's beam. Serialized: append only, values match Unity. */
UENUM(BlueprintType)
enum class ELaserBeamLook : uint8
{
	/** The LaserA / LaserB emitters and a drawn beam. */
	Procedural = 0,
	/** The laser model with its beam sheet; every gate is vertical. */
	Prefab = 1,
	Both = 2,
};

/** The four gate shapes, rolled per gate by weight. Serialized: append only, values match Unity. */
UENUM(BlueprintType)
enum class ELaserGateVariant : uint8
{
	Horizontal = 0,
	Vertical = 1,
	Triple = 2,
	Rotor = 3,
};

/**
 * A laser gate's beam, variants, emitters, look, wave and activation. A beam never spans the track
 * (CoverageBand of the lane): the way past is round. Played as a runtime copy.
 */
UCLASS(BlueprintType, meta = (UnityType = "LaserGateDefinition"))
class RUNNERCORE_API ULaserGateDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ beam
	/** Beam length as a fraction of the lane width (min, max), rolled per gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = 0.05, ClampMax = 0.6))
	FFloatInterval CoverageBand = FFloatInterval(0.2f, 0.3f);

	/** Half thickness of the beam that burns. The ship's own half extents are added on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = 0.1, ClampMax = 4, Units = "m"))
	float BeamRadius = 0.6f;

	/** Height of a horizontal beam's axis above the flight line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beam", meta = (ClampMin = -1, ClampMax = 5, Units = "m"))
	float BeamHeight = 0.f;

	// -------------------------------------------------------------- variants
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 0, ClampMax = 10))
	float HorizontalWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 0, ClampMax = 10))
	float VerticalWeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 0, ClampMax = 10))
	float TripleWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 0, ClampMax = 10))
	float RotorWeight = 1.f;

	/** Triple: metres between one beam and the one above it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 2, ClampMax = 20, Units = "m"))
	float TripleSpacing = 6.f;

	/** Vertical (procedural look only): how far above the flight line the beam reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 5, ClampMax = 60, Units = "m"))
	float VerticalHeight = 20.f;

	/** Rotor turn rate (min, max), degrees per second; the direction is a coin toss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variants", meta = (ClampMin = 10, ClampMax = 360, Units = "deg"))
	FFloatInterval RotorSpeedBand = FFloatInterval(45.f, 120.f);

	// -------------------------------------------------------------- emitters
	/** Scale of the emitter models (authored about 1.2 m across). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitters", meta = (ClampMin = 0.5, ClampMax = 20))
	float EmitterScale = 5.f;

	/** How fast the emitters roll about their barrel while firing, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitters", meta = (ClampMin = 0, ClampMax = 1440))
	float EmitterSpinDegPerSec = 180.f;

	/** Gap kept between the visible road and the lowest point an emitter can reach as it spins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emitters", meta = (ClampMin = 0, ClampMax = 50, Units = "m"))
	float EmitterRoadClearance = 0.5f;

	// ------------------------------------------------------------------ look
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	ELaserBeamLook Look = ELaserBeamLook::Prefab;

	/** Multiplier on the laser model, bar and sheet alike: never stretched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = 0.25, ClampMax = 20))
	float PrefabModelScale = 1.f;

	/** Additive material of the procedural beam. Empty = the default beam material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<UMaterialInterface> BeamMaterial;

	/** Colour of the wide outer glow (sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	FLinearColor BeamColor = FLinearColor(1.f, 0.1f, 0.1f);

	/** Colour of the thin hot core (sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	FLinearColor CoreColor = FLinearColor(1.f, 0.8f, 0.8f);

	/** Drawn width of the glow as a multiple of the burning thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = 0.5, ClampMax = 6))
	float GlowWidthFactor = 2.f;

	/** How much the beam's width flickers, as a fraction of itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = 0, ClampMax = 0.8))
	float FlickerAmount = 0.15f;

	/** Flickers per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = 1, ClampMax = 60, Units = "Hz"))
	float FlickerFrequency = 18.f;

	// ------------------------------------------------------------------ wave
	/** Beams may zigzag: a triangle wave running along the beam. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	bool bWavy = false;

	/** Share of the gates that get the wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (EditCondition = "bWavy", ClampMin = 0, ClampMax = 1))
	float WaveChance = 0.5f;

	/** How far the zigzag swings either side of the beam's axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (EditCondition = "bWavy", ClampMin = 0.1, ClampMax = 6, Units = "m"))
	float WaveAmplitude = 1.5f;

	/** Length of one full zig + zag along the beam. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (EditCondition = "bWavy", ClampMin = 1, ClampMax = 30, Units = "m"))
	float WaveLength = 8.f;

	/** Wavelengths per second, A to B. Negative runs it back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (EditCondition = "bWavy", ClampMin = -10, ClampMax = 10))
	float WaveSpeed = 1.f;

	/** Stretch at each muzzle over which the swing grows from nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (EditCondition = "bWavy", ClampMin = 0, ClampMax = 10, Units = "m"))
	float WaveTaper = 2.f;

	// ------------------------------------------------------------ activation
	/** Gates start off: the sheet lets itself down onto the road as the ship closes in (Prefab look only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	bool bActivation = true;

	/** Distance from the ship to the gate (Min = full, Max = starts down). Max must sit inside the reveal's seated edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (EditCondition = "bActivation", ClampMin = 0, ClampMax = 1500, Units = "m"))
	FFloatInterval GrowBand = FFloatInterval(450.f, 700.f);

	/** How far the ship must be past the gate before it pulls the sheet back up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (EditCondition = "bActivation", ClampMin = 0, ClampMax = 100, Units = "m"))
	float PassMargin = 20.f;

	/** Seconds the sheet takes to pull back into the bar once the ship is past. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (EditCondition = "bActivation", ClampMin = 0.05, ClampMax = 3, Units = "s"))
	float RetractSeconds = 0.4f;

	/** Share of its length the sheet must hang out before it counts as touching the road. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (EditCondition = "bActivation", ClampMin = 0.5, ClampMax = 1))
	float FloorContactThreshold = 0.95f;

	/** How much further the sheet keeps growing past the road, under the track. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (EditCondition = "bActivation", ClampMin = 0, ClampMax = 50, Units = "m"))
	float UnderTrackOverflow = 0.f;

	// Paired values are read through these, never as .Min / .Max.
	float GetGrowFullMeters() const { return FMath::Min(GrowBand.Min, GrowBand.Max); }
	float GetGrowStartMeters() const { return FMath::Max(GrowBand.Min, GrowBand.Max); }
	float GetCoverageMin() const { return FMath::Min(CoverageBand.Min, CoverageBand.Max); }
	float GetCoverageMax() const { return FMath::Max(CoverageBand.Min, CoverageBand.Max); }
	float GetRotorSpeedMin() const { return FMath::Min(RotorSpeedBand.Min, RotorSpeedBand.Max); }
	float GetRotorSpeedMax() const { return FMath::Max(RotorSpeedBand.Min, RotorSpeedBand.Max); }

	/** The look draws the emitters and the code-built beam. */
	bool DrawsProcedural() const { return Look != ELaserBeamLook::Prefab; }
	/** The look draws the authored laser model (which only stands up). */
	bool DrawsPrefab() const { return Look != ELaserBeamLook::Procedural; }

	/** Sum of the variant weights; 0 = no gate can be drawn. */
	float GetTotalWeight() const
	{
		return FMath::Max(0.f, HorizontalWeight) + FMath::Max(0.f, VerticalWeight) + FMath::Max(0.f, TripleWeight) + FMath::Max(0.f, RotorWeight);
	}

	/** One weighted draw over the four variants, Unity's order (horizontal, vertical, triple, rotor). */
	ELaserGateVariant PickVariant(float Roll01) const
	{
		float Pick = FMath::Clamp(Roll01, 0.f, 1.f) * GetTotalWeight();
		if ((Pick -= FMath::Max(0.f, HorizontalWeight)) < 0.f) { return ELaserGateVariant::Horizontal; }
		if ((Pick -= FMath::Max(0.f, VerticalWeight)) < 0.f) { return ELaserGateVariant::Vertical; }
		if ((Pick -= FMath::Max(0.f, TripleWeight)) < 0.f) { return ELaserGateVariant::Triple; }
		if (RotorWeight > 0.f) { return ELaserGateVariant::Rotor; }
		// The roll fell off the end of a table whose last weights are 0.
		if (TripleWeight > 0.f) { return ELaserGateVariant::Triple; }
		if (VerticalWeight > 0.f) { return ELaserGateVariant::Vertical; }
		return ELaserGateVariant::Horizontal;
	}
};
