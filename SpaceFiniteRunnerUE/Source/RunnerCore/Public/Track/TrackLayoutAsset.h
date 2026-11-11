#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Track/TrackLayout.h"
#include "TrackLayoutAsset.generated.h"

class UTrackShape;

/**
 * A saved track: a FTrackLayout the editor generated, the shape it was generated with and the
 * run's countdown. A level names one in its Track field and then plays exactly this track instead
 * of generating one. Gameplay never writes it: the generator loads a copy of the records.
 */
UCLASS(BlueprintType, meta = (UnityType = "TrackLayoutAsset"))
class RUNNERCORE_API UTrackLayoutAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Seconds on the run's countdown for this track. 0 = the game settings' time limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run", meta = (ClampMin = 0, ClampMax = 600, Units = "s"))
	float TimeLimitSeconds = 0.f;

	/** The shape asset the track was generated with: its feature table builds the ramps (records point into it by index), and Generate starts from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated With")
	TObjectPtr<UTrackShape> Shape;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Track")
	FTrackLayout Layout;

	bool IsValid() const { return Layout.IsValid(); }
};
