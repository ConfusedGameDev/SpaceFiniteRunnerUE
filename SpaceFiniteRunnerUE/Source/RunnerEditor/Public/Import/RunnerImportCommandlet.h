#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RunnerImportCommandlet.generated.h"

/**
 * Headless entry to FRunnerUnityImporter:
 *   UnrealEditor-Cmd <project> -run=RunnerImport [-ImportDir=<path>] [-Types=A,B] [-Overwrite] [-DryRun]
 * -Dump instead reloads the DataAssets under /Game/Runner/Data and writes them to Saved/RunnerImport/Dump as JSON.
 * The same import runs inside the editor from the console: Runner.ImportUnityData [Overwrite] [DryRun] [Types=A,B].
 */
UCLASS()
class RUNNEREDITOR_API URunnerImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URunnerImportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
