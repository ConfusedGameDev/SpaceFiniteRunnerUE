#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JumpRamp.generated.h"

class UJumpDefinition;
class UMaterialInterface;
class UStaticMesh;

/**
 * A spawned jump ramp (the Unity JumpRamp): where it sits on the track and what it does. Detection
 * is analytic, never a trigger: the ship knows its distance and lateral exactly and reads the live
 * ramps off the generator (ATrackGenerator::GetRamps), so a ship moving 20 m a step can't tunnel
 * through it. The cube slab and rails are only a picture; the collider builder gives it a wedge.
 * Metres, track space.
 */
UCLASS(NotBlueprintable)
class RUNNERCORE_API AJumpRamp : public AActor
{
	GENERATED_BODY()

public:
	AJumpRamp();

	void Configure(const UJumpDefinition* InDefinition, float InStartDistance, float InLateral, float InHalfWidth, float InBoost, bool bInEndRamp, float InRollBonus = 0.f);

	/** The code-built picture: a slab pitched to the ramp angle and a rail down each side. */
	void BuildPicture(UStaticMesh* Box, UMaterialInterface* Material);

	const UJumpDefinition* GetDefinition() const { return Definition; }
	float GetStartDistance() const { return StartDistance; }
	float GetLateral() const { return Lateral; }
	float GetHalfWidth() const { return HalfWidth; }
	/** Raw takeoff boost, m/s, before the ship's weight scaling. */
	float GetBoost() const { return Boost; }
	/** Extra share of the boost a barrel roll in the flight earns, paid on landing. */
	float GetRollBonus() const { return RollBonus; }
	/** One of the three ramps the track ends in: its lip is the end of the road. */
	bool IsEndRamp() const { return bEndRamp; }

	float GetLength() const;
	float GetEndDistance() const { return StartDistance + GetLength(); }

	/** True while Distance lies on the run-up. */
	bool Spans(float Distance) const { return Distance >= StartDistance && Distance < GetEndDistance(); }
	/** 0 at the foot, 1 at the lip. */
	float Progress(float Distance) const;
	/** Height of the slope above the flight line at Distance. */
	float HeightAt(float Distance) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<const UJumpDefinition> Definition;

	float StartDistance = 0.f;
	float Lateral = 0.f;
	float HalfWidth = 0.f;
	float Boost = 0.f;
	float RollBonus = 0.f;
	bool bEndRamp = false;
};
