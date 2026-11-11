#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Track/Spawning/TrackSpawner.h"
#include "Track/TrackLayout.h"
#include "TrackPlacementCatalog.generated.h"

/** One thing the track editor's palette offers to place. */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FTrackPlacementCatalogEntry
{
	GENERATED_BODY()

	/** The palette's label. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	FString DisplayName = TEXT("Power-up");

	/** SpeedOrb = a tier of the spawn set's speed orbs. RepairOrb = the repair orb. Pickup = a new power-up built from the pad below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	ETrackPlacementKind BuiltAs = ETrackPlacementKind::Pickup;

	/** SpeedOrb only: the tier (0 Green, 1 Blue, 2 Purple in the shipped set). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = 0, EditCondition = "BuiltAs == ETrackPlacementKind::SpeedOrb", EditConditionHides))
	int32 SpeedOrbTier = 0;

	/** Pickup only: the power-up (look, definition, boost multiplier, colour, sway, lane). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (EditCondition = "BuiltAs == ETrackPlacementKind::Pickup", EditConditionHides))
	FPadSpawnEntry Pad;

	/** The palette's and the gizmo's colour (sRGB, as authored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	FLinearColor Color = FLinearColor::White;
};

/**
 * What the track editor's palette offers to place: every power-up, as data. An entry is one of the
 * track's own kinds (a speed orb tier, the repair orb), built by the spawn set's spawners exactly as a
 * generated track builds them, or a new power-up built from a Pickup record that points at the entry
 * by index. Keep the order: Pickup records index it.
 */
UCLASS(BlueprintType, meta = (UnityType = "TrackPlacementCatalog"))
class RUNNERCORE_API UTrackPlacementCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	TArray<FTrackPlacementCatalogEntry> Entries;

	/** The record an entry places at a spot (its kind and variant). */
	FTrackPlacement RecordFor(int32 Index, float Distance, float Lateral) const
	{
		const FTrackPlacementCatalogEntry& E = Entries[Index];
		FTrackPlacement P;
		P.Distance = Distance;
		P.Lateral = Lateral;
		switch (E.BuiltAs)
		{
		case ETrackPlacementKind::SpeedOrb: P.Kind = ETrackPlacementKind::SpeedOrb; P.Variant = E.SpeedOrbTier; break;
		case ETrackPlacementKind::RepairOrb: P.Kind = ETrackPlacementKind::RepairOrb; break;
		default: P.Kind = ETrackPlacementKind::Pickup; P.Variant = Index; break;
		}
		return P;
	}
};
