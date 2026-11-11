#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class FProperty;
class UClass;
class UStruct;

/** What one import run is asked to do. */
struct RUNNEREDITOR_API FRunnerImportOptions
{
	/** Folder the Unity exporter wrote to. Empty = <Project>/Import. */
	FString ImportDir;

	/** Unity type names to import. Empty = every type that has a UE class. */
	TArray<FString> Types;

	/**
	 * Re-import assets that already exist. Off by default: once an asset is in the project it is
	 * tuned here, and a re-import would put the Unity values back.
	 */
	bool bOverwrite = false;

	/** Read and report, write nothing. */
	bool bDryRun = false;
};

/**
 * The Unreal half of the port's data pipeline: reads the JSON the Unity exporter wrote
 * (Import/Data/<UnityType>/<Asset>.json) and creates one DataAsset per file.
 *
 * - A UE class takes a Unity type by declaring meta=(UnityType="…") on its UCLASS.
 * - A property takes the Unity field of the same name, case-insensitively, with the bool "b"
 *   prefix dropped; meta=(UnityName="…") names a different field.
 * - Values stay in the units Unity stores (metres, m/s, degrees). Vectors that live in space
 *   say so with meta=(UnitySpace="Vector" | "Rotation") and are moved from Unity's axes
 *   (X right, Y up, Z forward) to Unreal's (X forward, Y right, Z up), unscaled.
 * - Object references resolve by Unity guid to the asset the same run creates or finds.
 * - Where assets go is decided by Import/ImportMap.json.
 *
 * Everything that could not be carried over (Unity fields with no property, properties with no
 * Unity value, references to assets not ported yet) is listed in Saved/RunnerImport/Report.txt.
 */
class RUNNEREDITOR_API FRunnerUnityImporter
{
public:
	explicit FRunnerUnityImporter(const FRunnerImportOptions& InOptions);

	/** Runs the import. Returns false when a file could not be read or an asset could not be saved. */
	bool Run();

	int32 GetCreatedCount() const { return Created; }
	int32 GetUpdatedCount() const { return Updated; }
	int32 GetSkippedCount() const { return Skipped; }
	const FString& GetReportPath() const { return ReportPath; }

private:
	struct FSource
	{
		FString File;
		FString UnityType;
		FString Name;
		FString Guid;
		FString ObjectPath;
		UClass* Class = nullptr;
		TSharedPtr<FJsonObject> Fields;
	};

	void LoadMap();
	void FindClasses();
	bool ReadSources();
	FString DestinationFor(const FSource& Source) const;
	bool ImportSource(const FSource& Source);

	void ImportStruct(const UStruct* Struct, void* Container, const TSharedPtr<FJsonObject>& Json, const FString& Context);
	void ImportValue(FProperty* Property, void* Value, const TSharedPtr<FJsonValue>& Json, const FString& Context);
	UObject* ResolveReference(const TSharedPtr<FJsonObject>& Json, const FString& Context);
	void Note(const FString& Line);
	void WriteReport();

	FRunnerImportOptions Options;
	FString DataDir;
	FString ReportPath;

	TMap<FString, UClass*> ClassByUnityType;
	TMap<FString, FString> PathByAssetName;
	TMap<FString, FString> FolderByUnityType;
	TMap<FString, FString> PrefixByUnityType;
	FString DefaultFolder = TEXT("/Game/Runner/Data");
	FString DefaultPrefix = TEXT("DA_");

	TArray<FSource> Sources;
	TMap<FString, FString> ObjectPathByGuid;
	TArray<FString> UnportedTypes;
	TArray<FString> Notes;

	int32 Created = 0;
	int32 Updated = 0;
	int32 Skipped = 0;
	bool bFailed = false;
};
