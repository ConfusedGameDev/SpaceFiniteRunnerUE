#include "Assets/RunnerAssetScriptLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

UFont* URunnerAssetScriptLibrary::BuildCompositeFont(const FString& PackagePath, UFontFace* Regular, UFontFace* Fallback)
{
	if (!Regular || !FPackageName::IsValidLongPackageName(PackagePath))
	{
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	const FName AssetName(*FPackageName::GetLongPackageAssetName(PackagePath));
	UFont* Font = FindObject<UFont>(Package, *AssetName.ToString());
	const bool bCreated = Font == nullptr;
	if (bCreated)
	{
		Font = NewObject<UFont>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}

	Font->Modify();
	Font->FontCacheType = EFontCacheType::Runtime;

	FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
	Composite = FCompositeFont();
	FTypefaceEntry& RegularEntry = Composite.DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Regular")));
	RegularEntry.Font = FFontData(Regular);
	if (Fallback)
	{
		FTypefaceEntry& FallbackEntry = Composite.FallbackTypeface.Typeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Regular")));
		FallbackEntry.Font = FFontData(Fallback);
	}
	Composite.MakeDirty();

	Font->PostEditChange();
	Font->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Font);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Font, *Filename, SaveArgs))
	{
		return nullptr;
	}
	return Font;
}
