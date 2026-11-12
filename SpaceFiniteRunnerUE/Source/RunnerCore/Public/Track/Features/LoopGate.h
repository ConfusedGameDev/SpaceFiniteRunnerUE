#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LoopGate.generated.h"

class FLoopSection;
class ULoopDefinition;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTextRenderComponent;

/**
 * A spawned loop (the Unity LoopFeature): the section the track routes through, the definition copy
 * and the entry speed fixed for it. Read analytically by the ship off the generator's live loops,
 * never through a trigger. The road round the loop is the decorator's like any other stretch; this
 * actor is the gate at the mouth, a portal frame recoloured against the ship's speed
 * (SetGateColor), with the required speed above it as a fixed label shown inside the alert lead.
 */
UCLASS(NotBlueprintable)
class RUNNERCORE_API ALoopGate : public AActor
{
	GENERATED_BODY()

public:
	ALoopGate();

	void Configure(const ULoopDefinition* InDefinition, const FLoopSection* InSection, float InRequiredSpeed);

	/** The code-built portal: two posts and a crossbar across the whole track, and the label above. */
	void BuildPicture(UStaticMesh* Box, UMaterialInterface* Material, float TrackHalfWidth, float SpeedDisplayMultiplier);

	const ULoopDefinition* GetDefinition() const { return Definition; }
	/** The section, valid while the track that made it stands (the generator culls this first). */
	const FLoopSection* GetSection() const { return Section; }
	/** Entry speed the loop demands, m/s. */
	float GetRequiredSpeed() const { return RequiredSpeed; }
	float GetStartDistance() const { return StartDistance; }
	float GetEndDistance() const { return EndDistance; }

	/** Pass (fast enough) or fail colour on the frame and the label. Cheap every frame: acts on a change only. */
	void SetGateColor(bool bPass);
	void SetLabelVisible(bool bVisible);
	bool IsLabelVisible() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<const ULoopDefinition> Definition;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GateMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> Label;

	const FLoopSection* Section = nullptr;
	float RequiredSpeed = 0.f;
	float StartDistance = 0.f;
	float EndDistance = 0.f;
	int8 GateState = -1; // -1 unset, 0 fail, 1 pass
};
