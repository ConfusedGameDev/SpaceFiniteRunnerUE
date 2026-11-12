#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RunnerPrefabLayout.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * One transform of a Unity prefab, flattened into the prefab ROOT's space (the exporter's
 * UnrealPrefabLayout). A node with a Model is an FBX instance, placed so the imported mesh (pivot at
 * its bounds centre) lands where Unity draws it; a node with a Mesh names a built-in (Plane, Cube, …);
 * every other node is an empty (ShootPoint, SheetPivot) kept so code can find it by path.
 */
USTRUCT(BlueprintType)
struct RUNNERCORE_API FRunnerPrefabNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	FString Name;

	/** Path under the root, '/'-separated (Unity's hierarchy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	FString Path;

	/** Active in the prefab (it and every parent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	bool bActive = true;

	/** Its renderer(s) are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	bool bRendered = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	TObjectPtr<UStaticMesh> Model;

	/** A built-in mesh by Unity name ("Plane", "Cube", "Sphere", "Cylinder", "Quad"), or "Sprite:<name>". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	FString Mesh;

	/** The Unity renderer materials, as imported. A Model keeps its own mesh's materials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	/** Root-space position, metres (Unreal axes after import). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (UnitySpace = "Vector"))
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (UnitySpace = "Rotation"))
	FQuat Rotation = FQuat::Identity;

	/** Root-space scale (a mirror shows as a negative Y, Unity's x). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (UnitySpace = "Vector"))
	FVector Scale = FVector::OneVector;

	/** The Unity scripts on it (what the Unreal code rebuilds them as). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	TArray<FString> Components;

	/** Root-space transform in centimetres. */
	FTransform GetTransform() const { return FTransform(Rotation, Position * 100.0, Scale); }
	bool IsDrawn() const { return bActive && bRendered && (Model != nullptr || !Mesh.IsEmpty()); }
};

/** A Unity prefab's picture, rebuilt from the exporter's layout. The importer keys it on the prefab's guid. */
UCLASS(BlueprintType, meta = (UnityType = "PrefabLayout"))
class RUNNERCORE_API URunnerPrefabLayout : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The prefab root's own scale (Instantiate keeps it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (UnitySpace = "Vector"))
	FVector RootScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FString> RootComponents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	TArray<FRunnerPrefabNode> Nodes;

	const FRunnerPrefabNode* FindNode(const FString& Path) const;
	/** The first node whose path ends in "/Name" or is Name, under a node whose path starts with Under (empty = anywhere). */
	const FRunnerPrefabNode* FindNamed(const FString& Name, const FString& Under = FString()) const;
};

namespace RunnerPrefab
{
	/** The engine mesh standing in for a Unity built-in, and the scale that matches its size (null when none does). */
	RUNNERCORE_API UStaticMesh* BuiltinMesh(const FString& UnityName, FVector& OutScaleFix);

	/** Mesh corners of a drawn node in root space, cm (for measuring a sheet). Empty when it has no mesh. */
	RUNNERCORE_API void NodeCorners(const FRunnerPrefabNode& Node, TArray<FVector>& Out);

	/**
	 * Adds one static mesh component per drawn node (Filter picks which) under Parent, at its root-space
	 * pose; Parent's own scale is the caller's (usually RootScale times a size). Pickup pictures only:
	 * the components have no collision. Extra places a node relative to its root-space pose (or null).
	 */
	RUNNERCORE_API void Assemble(const URunnerPrefabLayout& Layout, USceneComponent& Parent, TArray<UStaticMeshComponent*>& OutMeshes,
		TFunctionRef<bool(const FRunnerPrefabNode&)> Filter);
	RUNNERCORE_API void Assemble(const URunnerPrefabLayout& Layout, USceneComponent& Parent, TArray<UStaticMeshComponent*>& OutMeshes);

	/** One component for one node at an explicit root-space transform (cm). Null when it draws nothing. */
	RUNNERCORE_API UStaticMeshComponent* AddNodeMesh(const FRunnerPrefabNode& Node, USceneComponent& Parent, const FTransform& RootSpace);
}
