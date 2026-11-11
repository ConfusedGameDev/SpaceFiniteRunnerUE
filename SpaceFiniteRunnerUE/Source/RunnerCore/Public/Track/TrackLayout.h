#pragma once

#include "CoreMinimal.h"
#include "TrackLayout.generated.h"

/** What a FTrackPlacement builds. Serialized in track assets: append only, values match Unity. */
UENUM(BlueprintType)
enum class ETrackPlacementKind : uint8
{
	SpeedOrb = 0,
	RepairOrb = 1,
	LaserGate = 2,
	Ramp = 3,
	Collectible = 4,
	Pickup = 5,
	CustomPrefab = 6,
	EndRamp = 7,
	Loop = 8,
	/** Retired: kept so saved tracks keep their values. */
	BrakePad = 9,
	/** An Arcade time trial's checkpoint: built by the time-trial system, never by the generator. */
	Checkpoint = 10,
};

/** How a knot's tangents are derived. Values match Unity.Splines TangentMode. */
UENUM(BlueprintType)
enum class ETrackTangentMode : uint8
{
	AutoSmooth = 0,
	Linear = 1,
	Mirrored = 2,
	Continuous = 3,
	Broken = 4,
};

/**
 * One thing on the track, decided but not built: what it is, where it is (distance from the
 * track start, lateral and height, all in metres) and whatever was rolled for it.
 * Variant: the speed orb's tier, the laser gate's variant, the feature table index of a ramp or
 * a loop, the end ramp's slot (0 left, 1 middle, 2 right).
 * Data: the laser gate's (beam length, rotor speed, rotor phase, wavy 0/1).
 */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	ETrackPlacementKind Kind = ETrackPlacementKind::SpeedOrb;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (Units = "m"))
	float Distance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (Units = "m"))
	float Lateral = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (Units = "m"))
	float Height = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	int32 Variant = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FVector4 Data = FVector4(0, 0, 0, 0);
};

/** One spline knot in the generator's space: Unreal axes (X forward, Y right, Z up), metres. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackKnot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knot", meta = (UnitySpace = "Vector"))
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knot", meta = (UnitySpace = "Rotation"))
	FQuat Rotation = FQuat::Identity;

	/** In the knot's local space, as Unity.Splines stores it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knot", meta = (UnitySpace = "Vector"))
	FVector TangentIn = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knot", meta = (UnitySpace = "Vector"))
	FVector TangentOut = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knot")
	ETrackTangentMode Mode = ETrackTangentMode::AutoSmooth;
};

/** A loop or a tube: the feature table entry that made it, where it starts and the random state it rolled from. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackSectionRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section")
	int32 FeatureIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section", meta = (Units = "m"))
	float Start = 0.f;

	/** uint32 in Unity; int64 here so Blueprint can read it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section")
	int64 RngState = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section", meta = (Units = "m"))
	float SplineExtent = 0.f;
};

/** A stretch of track: a flat sweep (with its outer side) or an open straight. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackSpan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Span", meta = (Units = "m"))
	float Start = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Span", meta = (Units = "m"))
	float End = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Span")
	int32 OuterSide = 0;
};

/**
 * A whole decided track, as data: the knots the spline is rebuilt from, every knot's track
 * distance, the sections, the dangerous spans, the keep-outs the patrol's duel reads, the end and
 * every placement. Nothing here is built: road and collision are always derived from it at
 * runtime, inside the stream window. Distances are metres from the track start.
 */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackLayout
{
	GENERATED_BODY()

	static constexpr int32 CurrentVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	int32 FormatVersion = CurrentVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	int64 Seed = 0;

	/** Full road width. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Units = "m"))
	float Width = 0.f;

	/** Track length (spline + inserted sections) when captured; a load checks it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Units = "m"))
	float Length = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Units = "m"))
	float EndZoneStart = -1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Units = "m"))
	float EndDistance = -1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Units = "m"))
	float EndRunUp = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FTrackKnot> Knots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<float> KnotDistances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FTrackSectionRecord> Sections;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FTrackSpan> FlatSweeps;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FTrackSpan> OpenStretches;

	/** (start, end) distances the patrol's duel keeps out of. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FFloatInterval> KeepOuts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FTrackPlacement> Placements;

	bool IsValid() const
	{
		return Knots.Num() >= 2 && KnotDistances.Num() == Knots.Num() && EndDistance > 0.f;
	}
};
