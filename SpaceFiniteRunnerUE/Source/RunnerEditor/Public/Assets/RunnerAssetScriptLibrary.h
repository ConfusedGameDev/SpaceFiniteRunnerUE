#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RunnerAssetScriptLibrary.generated.h"

class UFont;
class UFontFace;

/**
 * Editor-only helpers for the asset scripts in Tools/Python (M1), for the few engine fields Python cannot
 * reach because they are not exposed to reflection-based scripting.
 */
UCLASS()
class RUNNEREDITOR_API URunnerAssetScriptLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds) a runtime-cached composite UFont at PackagePath whose default typeface is Regular
	 * and whose fallback typeface is Fallback, so glyphs the regular face lacks (Japanese, œ, –) still draw.
	 * Returns the saved font, or null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Runner|Assets")
	static UFont* BuildCompositeFont(const FString& PackagePath, UFontFace* Regular, UFontFace* Fallback);
};
