#include "Import/RunnerImportCommandlet.h"

#include "Import/RunnerUnityImporter.h"
#include "RunnerEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

namespace
{
	FRunnerImportOptions ParseOptions(const FString& Params)
	{
		FRunnerImportOptions Options;
		FParse::Value(*Params, TEXT("ImportDir="), Options.ImportDir);
		Options.bOverwrite = FParse::Param(*Params, TEXT("Overwrite"));
		Options.bDryRun = FParse::Param(*Params, TEXT("DryRun"));

		FString Types;
		if (FParse::Value(*Params, TEXT("Types="), Types, false))
		{
			Types.ParseIntoArray(Options.Types, TEXT(","), true);
		}
		return Options;
	}

	/** Reloads every DataAsset under /Game/Runner/Data and writes it as JSON with the engine's own serializer: the importer's result, read back without the importer. */
	int32 DumpDataAssets()
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		Registry.Get().SearchAllAssets(true);

		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByPath(TEXT("/Game/Runner/Data"), Assets, true);

		const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RunnerImport"), TEXT("Dump"));
		int32 Written = 0;
		for (const FAssetData& Data : Assets)
		{
			const UDataAsset* Asset = Cast<UDataAsset>(Data.GetAsset());
			FString Json;
			if (Asset != nullptr && FJsonObjectConverter::UStructToJsonObjectString(Asset->GetClass(), Asset, Json))
			{
				FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Folder, Asset->GetName() + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				++Written;
			}
		}
		UE_LOG(LogRunnerEditor, Display, TEXT("RunnerImport dump: %d assets written to %s"), Written, *FPaths::ConvertRelativePathToFull(Folder));
		return Written;
	}

	FAutoConsoleCommand ImportUnityDataCommand(
		TEXT("Runner.ImportUnityData"),
		TEXT("Creates DataAssets from the Unity exporter's JSON in <Project>/Import. Arguments: Overwrite, DryRun, Types=A,B."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// Console arguments come bare; the parser wants switches.
			FString Params;
			for (const FString& Arg : Args)
			{
				Params += TEXT(" -") + Arg;
			}
			FRunnerUnityImporter Importer(ParseOptions(Params));
			Importer.Run();
		}));
}

URunnerImportCommandlet::URunnerImportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URunnerImportCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("Dump")))
	{
		return DumpDataAssets() > 0 ? 0 : 1;
	}

	FRunnerUnityImporter Importer(ParseOptions(Params));
	const bool bSucceeded = Importer.Run();
	UE_LOG(LogRunnerEditor, Display, TEXT("RunnerImport %s: %d created, %d updated, %d skipped."),
		bSucceeded ? TEXT("finished") : TEXT("FAILED"), Importer.GetCreatedCount(), Importer.GetUpdatedCount(), Importer.GetSkippedCount());
	return bSucceeded ? 0 : 1;
}
