#include "Prefabs/RunnerPrefabLayout.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

const FRunnerPrefabNode* URunnerPrefabLayout::FindNode(const FString& Path) const
{
	return Nodes.FindByPredicate([&Path](const FRunnerPrefabNode& N) { return N.Path == Path; });
}

const FRunnerPrefabNode* URunnerPrefabLayout::FindNamed(const FString& Name, const FString& Under) const
{
	return Nodes.FindByPredicate([&](const FRunnerPrefabNode& N)
	{
		return N.Name == Name && (Under.IsEmpty() || N.Path.StartsWith(Under + TEXT("/")));
	});
}

namespace RunnerPrefab
{
	UStaticMesh* BuiltinMesh(const FString& UnityName, FVector& OutScaleFix)
	{
		// Unity's built-ins after the axis move (x, y, z) → (z, x, y): the plane (10 m, in Unity's XZ) lies
		// in Unreal's XY facing +Z like the engine's 1 m plane; the cylinder is 2 m along Unity's y (Unreal Z).
		const TCHAR* Path = nullptr;
		OutScaleFix = FVector::OneVector;
		if (UnityName == TEXT("Plane")) { Path = TEXT("/Engine/BasicShapes/Plane.Plane"); OutScaleFix = FVector(10.0, 10.0, 1.0); }
		else if (UnityName == TEXT("Cube")) { Path = TEXT("/Engine/BasicShapes/Cube.Cube"); }
		else if (UnityName == TEXT("Sphere")) { Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); }
		else if (UnityName == TEXT("Cylinder")) { Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); OutScaleFix = FVector(1.0, 1.0, 2.0); }
		// Quad and sprites have no stand-in yet (the QTE prompt is the HUD's, M5/M11).
		return Path ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr;
	}

	namespace
	{
		UStaticMesh* NodeMesh(const FRunnerPrefabNode& Node, FVector& OutFix)
		{
			OutFix = FVector::OneVector;
			if (Node.Model) return Node.Model;
			return Node.Mesh.IsEmpty() ? nullptr : BuiltinMesh(Node.Mesh, OutFix);
		}
	}

	void NodeCorners(const FRunnerPrefabNode& Node, TArray<FVector>& Out)
	{
		Out.Reset();
		FVector Fix;
		UStaticMesh* Mesh = NodeMesh(Node, Fix);
		if (!Mesh) return;
		const FBox Box = Mesh->GetBoundingBox();
		const FTransform T = FTransform(FQuat::Identity, FVector::ZeroVector, Fix) * Node.GetTransform();
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector C((i & 1) ? Box.Max.X : Box.Min.X, (i & 2) ? Box.Max.Y : Box.Min.Y, (i & 4) ? Box.Max.Z : Box.Min.Z);
			Out.Add(T.TransformPosition(C));
		}
	}

	UStaticMeshComponent* AddNodeMesh(const FRunnerPrefabNode& Node, USceneComponent& Parent, const FTransform& RootSpace)
	{
		FVector Fix;
		UStaticMesh* Mesh = NodeMesh(Node, Fix);
		AActor* Owner = Parent.GetOwner();
		if (!Mesh || !Owner) return nullptr;

		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetMobility(Parent.Mobility);
		C->SetupAttachment(&Parent);
		C->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, Fix) * RootSpace);
		if (!Node.Model)
		{
			for (int32 i = 0; i < Node.Materials.Num(); ++i)
			{
				if (Node.Materials[i]) C->SetMaterial(i, Node.Materials[i]);
			}
		}
		Owner->AddInstanceComponent(C);
		C->RegisterComponent();
		return C;
	}

	void Assemble(const URunnerPrefabLayout& Layout, USceneComponent& Parent, TArray<UStaticMeshComponent*>& OutMeshes,
		TFunctionRef<bool(const FRunnerPrefabNode&)> Filter)
	{
		for (const FRunnerPrefabNode& Node : Layout.Nodes)
		{
			if (!Node.IsDrawn() || !Filter(Node)) continue;
			if (UStaticMeshComponent* C = AddNodeMesh(Node, Parent, Node.GetTransform())) OutMeshes.Add(C);
		}
	}

	void Assemble(const URunnerPrefabLayout& Layout, USceneComponent& Parent, TArray<UStaticMeshComponent*>& OutMeshes)
	{
		Assemble(Layout, Parent, OutMeshes, [](const FRunnerPrefabNode&) { return true; });
	}
}
