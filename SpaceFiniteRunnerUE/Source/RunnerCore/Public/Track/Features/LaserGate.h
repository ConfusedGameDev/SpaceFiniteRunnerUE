#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PickupContracts.h"
#include "TrackContracts.h"
#include "Track/Features/LaserGateDefinition.h"
#include "LaserGate.generated.h"

class UMaterialInterface;
class URunnerPrefabLayout;
class UStaticMeshComponent;

/**
 * One laser gate (the Unity LaserGate): one to three beams, each a SEGMENT in the gate's own track
 * space (X along the track with the gate's distance at 0, Y across, Z above the flight line,
 * metres). Detection is analytic: the gate sits in the UPickupRegistry as the box round all its beams
 * (the broad phase), and the beam test itself is M5. The rotor's angle is a pure function of scaled
 * time, shared by the picture and the test.
 *
 * The picture: the Prefab look poses the authored laser model (laserTypeA and its sheet, from the
 * exporter's PF_LaserSystem layout) on the vertical beam's top, the sheet hanging to the road and a
 * copy turned half round the beam for its back; the sheet is drawn fully down (the activation that
 * lets it down as the ship closes, and the floor sparks, are M5). Every other case draws a stand-in
 * rod per beam. Horizontal beams are drawn VisualLift higher than they burn, so the emitters clear
 * the visible road.
 */
UCLASS(NotBlueprintable)
class RUNNERCORE_API ALaserGate : public AActor, public ITrackPickup, public ITrackPositioned
{
	GENERATED_BODY()

public:
	ALaserGate();

	/**
	 * Shapes the gate. The actor already stands on the track pose at Distance, lateral 0; RoadSurface is
	 * the visible road's height relative to it (negative: below). Layout is the laser system's prefab
	 * (null = stand-in rods); RodMaterial tints the rods.
	 */
	void Configure(const ULaserGateDefinition& Definition, ELaserGateVariant InVariant, float Distance, float Lateral, float Length,
		float RotorDegreesPerSecond, float RotorPhaseDegrees, float RoadSurface, bool bInWavy, const URunnerPrefabLayout* Layout,
		UMaterialInterface* RodMaterial);

	ELaserGateVariant GetVariant() const { return Variant; }
	int32 GetBeamCount() const { return Beams.Num(); }
	/** Beam Index's segment in gate space at Time (seconds, scaled game time). */
	void GetSegment(int32 Index, float Time, FVector& OutA, FVector& OutB) const;
	/** Half thickness of what burns: the definition's, or the authored sheet's half width. */
	float GetBurnRadius() const { return BurnRadius; }
	/** How far the gate reaches along the track either side of its distance. */
	float GetHalfDepth() const { return BoundsHalf.X; }
	/** How far an emitter's body reaches past a beam's end, across the track, metres. */
	float GetEmitterReach() const;
	/** Metres the beams are drawn above the segments that burn. */
	float GetVisualLift() const { return VisualLift; }
	/** The authored model is drawn. */
	bool DrawsPrefabModel() const { return bPrefabDrawn; }
	/** How far the authored sheet reaches from its bar, metres (0 when not drawn). */
	float GetPrefabSheetLength() const { return SheetLength; }

	// ITrackPickup: the broad-phase box.
	virtual float GetPickupDistance() const override { return TrackDistance + BoundsCentre.X; }
	virtual float GetPickupLateral() const override { return BoundsCentre.Y; }
	virtual float GetPickupHeight() const override { return BoundsCentre.Z; }
	virtual FVector3f GetPickupHalfExtents() const override { return FVector3f(BoundsHalf); }
	virtual bool IsPickupAvailable() const override { return bPlaced; }

	virtual float GetTrackDistance() const override { return TrackDistance; }

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FBeam
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
	};

	bool BuildPrefabPicture(const URunnerPrefabLayout& Layout, float RoadSurface, float Lateral);
	void BuildRods(UMaterialInterface* Material);
	void PoseRods(float Time);
	float LiftClearOfRoad(float RoadSurface, float EmitterRadius) const;
	void ComputeBounds();

	UPROPERTY(Transient)
	TObjectPtr<const ULaserGateDefinition> Definition;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Rods;

	TArray<FBeam> Beams;
	ELaserGateVariant Variant = ELaserGateVariant::Horizontal;
	float TrackDistance = 0.f;
	bool bPlaced = false;
	bool bRotor = false;
	bool bPrefabDrawn = false;
	FVector RotorCentre = FVector::ZeroVector;
	float RotorHalfLength = 0.f;
	float RotorSpeed = 0.f;
	float RotorPhase = 0.f;
	FVector BoundsCentre = FVector::ZeroVector;
	FVector BoundsHalf = FVector::ZeroVector;
	float WaveAmplitude = 0.f;
	float BurnRadius = 0.f;
	float VisualLift = 0.f;
	float SheetLength = 0.f;
	float SheetHalfWidth = 0.f;
};
