#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Track/TrackLayout.h"
#include "TrackGenerator.generated.h"

class AJumpRamp;
class ALoopGate;
class IShipPerformance;
class IStreamFocus;
class ITrackRunRules;
class UJumpDefinition;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTrackColliderBuilder;
class UTrackDecider;
class UTrackDecorator;
class UTrackLayoutAsset;
class UTrackModel;
class UTrackShape;
class UTrackSpawnSet;
struct FTrackFeatureEntry;

DECLARE_MULTICAST_DELEGATE(FOnTrackRegenerated);
/** Raised for every actor a placement record builds: (actor, record distance, lateral). The reveal listens. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPlacementBuilt, AActor*, float, float);

/**
 * The BUILD half of the Unity TrackGenerator, and the track's actor. A UTrackDecider decides the
 * whole track at run start (or loads a saved one); this streams it: every record before the window's
 * edge (focus + AheadDistance) is built, the decorator stamps the road and the collider builder the
 * floor up to it, and everything that ends behind focus − BehindDistance goes. Building draws no
 * random numbers, so the same records always build the same track.
 *
 * Ramps, loops and the end ramps are built here; every other record goes to the spawner of its kind.
 * The live ramps and loops are read off GetRamps / GetLoops (the ship's analytic checks), never off a
 * static list or a world search (invariant 6). Track space is metres relative to this actor.
 */
UCLASS()
class RUNNERCORE_API ATrackGenerator : public AActor
{
	GENERATED_BODY()

public:
	ATrackGenerator();

	// ------------------------------------------------------------- what
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	TObjectPtr<UTrackShape> Shape;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	TObjectPtr<UTrackSpawnSet> SpawnSet;

	/** The three end ramps' definition. Empty = built-in numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	TObjectPtr<UJumpDefinition> EndRamp;

	/** A saved track to play instead of generating one. The run's rules override it when they name one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	TObjectPtr<UTrackLayoutAsset> Layout;

	/** 0 = a different layout every run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	int32 Seed = 0;

	/** Generate (or load) at BeginPlay. Off: something calls BuildTrack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	bool bBuildOnBeginPlay = true;

	// ------------------------------------------------------------ stream
	/** How far ahead of the focus everything is built. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = 100, Units = "m"))
	float AheadDistance = 700.f;

	/** How far behind the focus things are kept. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = 0, Units = "m"))
	float BehindDistance = 300.f;

	/** Pad footprint (width, thickness, length) the spawners scale from, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	FVector3f PadSize = FVector3f(10.f, 0.5f, 20.f);

	// ------------------------------------------------------------ looks
	/** The unit cube the code-built ramps and loop gates are made of. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Features")
	TSoftObjectPtr<UStaticMesh> FeatureBoxMesh;

	/** Tinted per feature entry (BaseColor, EmissionColor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Features")
	TSoftObjectPtr<UMaterialInterface> FeatureMaterial;

	// -------------------------------------------------------- components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrackDecorator> Decorator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrackColliderBuilder> Colliders;

	// ------------------------------------------------------------- api
	/** The collaborators, by contract only (the game mode binds them). Any may be null. */
	void Bind(IStreamFocus* InFocus, IShipPerformance* InPerformance, const ITrackRunRules* InRules);

	/** Decides (or loads) the whole track and builds the window's first stretch. False when nothing could be laid. */
	bool BuildTrack();

	/** Builds up to Focus + AheadDistance and culls behind Focus − BehindDistance. Tick calls it off the bound focus. */
	void StreamTo(float FocusDistance);

	/** Brings the end in now (the hyperspace jump): see UTrackDecider::ForceEndAhead. Drops the unbuilt records past the cut. */
	bool ForceEndAhead(float RunUpMeters);

	UTrackDecider* GetDecider() const { return Decider; }
	UTrackModel* GetTrack() const;
	/** Track distance everything is built to (the window's edge). */
	float GetBuiltTo() const { return BuiltTo; }
	/** Everything that ends before this is behind the focus and gone. */
	float GetCulledTo() const { return CulledTo; }

	const TArray<TObjectPtr<AJumpRamp>>& GetRamps() const { return Ramps; }
	const TArray<TObjectPtr<ALoopGate>>& GetLoops() const { return Loops; }
	/** The live ramp spanning Distance, or null. */
	AJumpRamp* RampAt(float Distance) const;

	FOnTrackRegenerated OnRegenerated;
	FOnPlacementBuilt OnPlacementBuilt;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ClearBuilt();
	void QueueNewPlacements();
	void BuildUpTo(float Edge);
	void CullBehind(float MinDistance);
	void Build(const FTrackPlacement& Placement);
	AJumpRamp* BuildRamp(float Distance, float Lateral, float HalfWidth, const UJumpDefinition& Def, UMaterialInterface* Material, float Boost, bool bEndRamp);
	void BuildJump(const FTrackPlacement& Placement);
	void BuildLoop(const FTrackPlacement& Placement);
	void BuildEndRamp(const FTrackPlacement& Placement);
	UMaterialInterface* EntryMaterial(int32 EntryIndex, const FLinearColor& Color);
	template <class T> T* SpawnOnTrack(float Distance, float Lateral);
	void Remember(AActor* Actor, float CullKey, const FTrackPlacement& Placement);

	UPROPERTY(Transient)
	TObjectPtr<UTrackDecider> Decider;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AJumpRamp>> Ramps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ALoopGate>> Loops;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> EntryMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Box;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseFeatureMaterial;

	IStreamFocus* Focus = nullptr;
	IShipPerformance* Performance = nullptr;
	const ITrackRunRules* Rules = nullptr;

	/** Decided records not built yet, by distance. */
	TArray<FTrackPlacement> Unbuilt;
	float BuiltTo = 0.f;
	float CulledTo = 0.f;
};
