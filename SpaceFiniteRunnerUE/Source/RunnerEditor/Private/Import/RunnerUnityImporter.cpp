#include "Import/RunnerUnityImporter.h"

#include "RunnerEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName UnityTypeMeta(TEXT("UnityType"));
	const FName UnityNameMeta(TEXT("UnityName"));
	const FName UnitySpaceMeta(TEXT("UnitySpace"));

	TSharedPtr<FJsonObject> LoadJsonFile(const FString& File)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	/** The Unity field name a property answers to, lower-cased: its UnityName, or its own name without the bool prefix. */
	FString UnityKeyOf(const FProperty* Property)
	{
		if (Property->HasMetaData(UnityNameMeta))
		{
			return Property->GetMetaData(UnityNameMeta).ToLower();
		}
		FString Name = Property->GetName();
		if (Property->IsA<FBoolProperty>() && Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1]))
		{
			Name.RightChopInline(1);
		}
		return Name.ToLower();
	}

	double Component(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		double Value = 0.0;
		Json->TryGetNumberField(Name, Value);
		return Value;
	}

	/** Unity (X right, Y up, Z forward) → Unreal (X forward, Y right, Z up). A cyclic swap, so handedness and lengths hold. */
	FVector FromUnityAxes(const FVector& Unity)
	{
		return FVector(Unity.Z, Unity.X, Unity.Y);
	}
}

FRunnerUnityImporter::FRunnerUnityImporter(const FRunnerImportOptions& InOptions)
	: Options(InOptions)
{
	if (Options.ImportDir.IsEmpty())
	{
		Options.ImportDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Import"));
	}
	Options.ImportDir = FPaths::ConvertRelativePathToFull(Options.ImportDir);
	DataDir = FPaths::Combine(Options.ImportDir, TEXT("Data"));
	ReportPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RunnerImport"), TEXT("Report.txt")));
}

bool FRunnerUnityImporter::Run()
{
	LoadMap();
	FindClasses();
	if (!ReadSources())
	{
		WriteReport();
		return false;
	}

	for (const FSource& Source : Sources)
	{
		if (!ImportSource(Source))
		{
			bFailed = true;
		}
	}

	WriteReport();
	UE_LOG(LogRunnerEditor, Display, TEXT("Unity import: %d created, %d updated, %d skipped (already in the project). Report: %s"),
		Created, Updated, Skipped, *ReportPath);
	return !bFailed;
}

void FRunnerUnityImporter::LoadMap()
{
	const FString File = FPaths::Combine(Options.ImportDir, TEXT("ImportMap.json"));
	const TSharedPtr<FJsonObject> Map = LoadJsonFile(File);
	if (!Map.IsValid())
	{
		Note(FString::Printf(TEXT("ImportMap.json not found or not valid JSON at %s: every asset goes to %s."), *File, *DefaultFolder));
		return;
	}

	Map->TryGetStringField(TEXT("defaultFolder"), DefaultFolder);
	Map->TryGetStringField(TEXT("defaultPrefix"), DefaultPrefix);

	const TSharedPtr<FJsonObject>* Types = nullptr;
	if (Map->TryGetObjectField(TEXT("types"), Types))
	{
		for (const auto& Pair : (*Types)->Values)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (Pair.Value->TryGetObject(Entry))
			{
				FString Folder, Prefix;
				if ((*Entry)->TryGetStringField(TEXT("folder"), Folder)) FolderByUnityType.Add(FString(*Pair.Key), Folder);
				if ((*Entry)->TryGetStringField(TEXT("prefix"), Prefix)) PrefixByUnityType.Add(FString(*Pair.Key), Prefix);
			}
		}
	}

	const TSharedPtr<FJsonObject>* Assets = nullptr;
	if (Map->TryGetObjectField(TEXT("assets"), Assets))
	{
		for (const auto& Pair : (*Assets)->Values)
		{
			FString Path;
			if (Pair.Value->TryGetString(Path)) PathByAssetName.Add(FString(*Pair.Key), Path);
		}
	}
}

void FRunnerUnityImporter::FindClasses()
{
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UDataAsset::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract) && Class->HasMetaData(UnityTypeMeta))
		{
			ClassByUnityType.Add(Class->GetMetaData(UnityTypeMeta), Class);
		}
	}
}

bool FRunnerUnityImporter::ReadSources()
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *DataDir, TEXT("*.json"), true, false);
	Files.Sort();
	if (Files.IsEmpty())
	{
		Note(FString::Printf(TEXT("No exported data under %s. Run Tools > Unreal Export > Export All in the Unity project first."), *DataDir));
		UE_LOG(LogRunnerEditor, Error, TEXT("Unity import: no JSON found under %s"), *DataDir);
		return false;
	}

	bool bAllRead = true;
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> Root = LoadJsonFile(File);
		const TSharedPtr<FJsonObject>* Fields = nullptr;
		FSource Source;
		Source.File = File;
		if (!Root.IsValid() || !Root->TryGetStringField(TEXT("unityType"), Source.UnityType) || !Root->TryGetStringField(TEXT("name"), Source.Name)
			|| !Root->TryGetStringField(TEXT("guid"), Source.Guid) || !Root->TryGetObjectField(TEXT("fields"), Fields))
		{
			Note(FString::Printf(TEXT("UNREADABLE %s"), *File));
			UE_LOG(LogRunnerEditor, Error, TEXT("Unity import: cannot read %s"), *File);
			bAllRead = false;
			continue;
		}

		Source.Fields = *Fields;
		Source.Class = ClassByUnityType.FindRef(Source.UnityType);
		if (Source.Class == nullptr)
		{
			UnportedTypes.AddUnique(Source.UnityType);
			continue;
		}

		// Every asset with a class gets its address now, so references resolve whatever the import order or filter.
		Source.ObjectPath = DestinationFor(Source);
		ObjectPathByGuid.Add(Source.Guid, Source.ObjectPath);

		if (Options.Types.IsEmpty() || Options.Types.ContainsByPredicate([&Source](const FString& Type) { return Type.Equals(Source.UnityType, ESearchCase::IgnoreCase); }))
		{
			Sources.Add(MoveTemp(Source));
		}
	}
	return bAllRead;
}

FString FRunnerUnityImporter::DestinationFor(const FSource& Source) const
{
	FString PackageName;
	if (const FString* Explicit = PathByAssetName.Find(Source.Name))
	{
		PackageName = *Explicit;
	}
	else
	{
		const FString* Folder = FolderByUnityType.Find(Source.UnityType);
		const FString* Prefix = PrefixByUnityType.Find(Source.UnityType);
		FString AssetName = Source.Name;
		for (TCHAR& Char : AssetName)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_')) Char = TEXT('_');
		}
		PackageName = (Folder ? *Folder : DefaultFolder) / ((Prefix ? *Prefix : DefaultPrefix) + AssetName);
	}
	return PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
}

bool FRunnerUnityImporter::ImportSource(const FSource& Source)
{
	const FString PackageName = FPackageName::ObjectPathToPackageName(Source.ObjectPath);
	const FString AssetName = FPackageName::GetShortName(PackageName);
	const bool bExists = FPackageName::DoesPackageExist(PackageName);

	if (bExists && !Options.bOverwrite)
	{
		++Skipped;
		return true;
	}

	Note(FString::Printf(TEXT("%s %s  <-  %s (%s)"), bExists ? TEXT("UPDATE") : TEXT("CREATE"), *Source.ObjectPath, *Source.Name, *Source.UnityType));
	if (Options.bDryRun)
	{
		// Still walk the data on a scratch object, so the report lists what would not carry over.
		UObject* Scratch = NewObject<UObject>(GetTransientPackage(), Source.Class);
		ImportStruct(Source.Class, Scratch, Source.Fields, Source.Name);
		return true;
	}

	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UObject* Asset = StaticFindObject(Source.Class, Package, *AssetName);
	if (Asset == nullptr)
	{
		if (UObject* Other = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
		{
			Note(FString::Printf(TEXT("  FAILED: %s already holds a %s, not a %s."), *PackageName, *Other->GetClass()->GetName(), *Source.Class->GetName()));
			return false;
		}
		Asset = NewObject<UObject>(Package, Source.Class, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
	}

	ImportStruct(Source.Class, Asset, Source.Fields, Source.Name);
	Asset->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
	{
		Note(FString::Printf(TEXT("  FAILED to save %s"), *FileName));
		UE_LOG(LogRunnerEditor, Error, TEXT("Unity import: could not save %s"), *FileName);
		return false;
	}

	bExists ? ++Updated : ++Created;
	return true;
}

void FRunnerUnityImporter::ImportStruct(const UStruct* Struct, void* Container, const TSharedPtr<FJsonObject>& Json, const FString& Context)
{
	// Unity field names, lower-cased, so matching ignores the two engines' casing rules.
	TMap<FString, FString> JsonKeys;
	for (const auto& Pair : Json->Values)
	{
		const FString JsonName(*Pair.Key);
		if (!JsonName.StartsWith(TEXT("$")))
		{
			JsonKeys.Add(JsonName.ToLower(), JsonName);
		}
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}
		// The engine's own DataAsset properties (asset bundle data) have no Unity side.
		if (const UClass* Owner = Property->GetOwnerClass(); Owner == UDataAsset::StaticClass() || Owner == UPrimaryDataAsset::StaticClass())
		{
			continue;
		}

		const FString Key = UnityKeyOf(Property);
		FString JsonKey;
		if (!JsonKeys.RemoveAndCopyValue(Key, JsonKey))
		{
			Note(FString::Printf(TEXT("  no Unity value   %s.%s (kept its default)"), *Context, *Property->GetName()));
			continue;
		}
		ImportValue(Property, Property->ContainerPtrToValuePtr<void>(Container), Json->TryGetField(JsonKey), Context + TEXT(".") + Property->GetName());
	}

	for (const auto& Left : JsonKeys)
	{
		Note(FString::Printf(TEXT("  no UE property   %s.%s"), *Context, *Left.Value));
	}
}

void FRunnerUnityImporter::ImportValue(FProperty* Property, void* Value, const TSharedPtr<FJsonValue>& Json, const FString& Context)
{
	if (!Json.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Object = nullptr;
	const bool bIsObject = Json->Type == EJson::Object && Json->TryGetObject(Object);

	// Enums: the exporter writes { "$enum": name, "value": number }.
	UEnum* Enum = nullptr;
	FNumericProperty* EnumStorage = nullptr;
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Enum = EnumProperty->GetEnum();
		EnumStorage = EnumProperty->GetUnderlyingProperty();
	}
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
	{
		Enum = ByteProperty->Enum;
		EnumStorage = ByteProperty;
	}
	if (Enum != nullptr)
	{
		int64 Number = 0;
		FString Name;
		if (bIsObject)
		{
			(*Object)->TryGetStringField(TEXT("$enum"), Name);
			Number = static_cast<int64>(Component(*Object, TEXT("value")));
		}
		else
		{
			Number = static_cast<int64>(Json->AsNumber());
		}

		// Serialized enums are append-only with matching values, so the number is the truth; the name is the cross-check.
		if (!Enum->IsValidEnumValue(Number))
		{
			Note(FString::Printf(TEXT("  bad enum value   %s = %lld (%s) is not in %s"), *Context, Number, *Name, *Enum->GetName()));
			return;
		}
		const FString UnrealName = Enum->GetNameStringByValue(Number);
		if (!Name.IsEmpty() && !UnrealName.Equals(Name.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase))
		{
			Note(FString::Printf(TEXT("  enum name differs  %s: Unity '%s' = %lld is '%s' in %s"), *Context, *Name, Number, *UnrealName, *Enum->GetName()));
		}
		EnumStorage->SetIntPropertyValue(Value, Number);
		return;
	}

	if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
	{
		Bool->SetPropertyValue(Value, Json->Type == EJson::Boolean ? Json->AsBool() : Json->AsNumber() != 0.0);
		return;
	}
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Json->Type != EJson::Number && Json->Type != EJson::Boolean)
		{
			Note(FString::Printf(TEXT("  type mismatch    %s expects a number"), *Context));
			return;
		}
		const double Number = Json->AsNumber();
		if (Numeric->IsFloatingPoint()) Numeric->SetFloatingPointPropertyValue(Value, Number);
		else Numeric->SetIntPropertyValue(Value, static_cast<int64>(Number));
		return;
	}
	if (const FStrProperty* Str = CastField<FStrProperty>(Property))
	{
		Str->SetPropertyValue(Value, Json->AsString());
		return;
	}
	if (const FNameProperty* Name = CastField<FNameProperty>(Property))
	{
		Name->SetPropertyValue(Value, FName(*Json->AsString()));
		return;
	}
	if (const FTextProperty* Text = CastField<FTextProperty>(Property))
	{
		Text->SetPropertyValue(Value, FText::FromString(Json->AsString()));
		return;
	}

	if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Json->TryGetArray(Items))
		{
			Note(FString::Printf(TEXT("  type mismatch    %s expects an array"), *Context));
			return;
		}
		FScriptArrayHelper Helper(Array, Value);
		Helper.EmptyAndAddValues(Items->Num());
		for (int32 Index = 0; Index < Items->Num(); ++Index)
		{
			// Element notes name the first element only: a thousand placements would say the same thing a thousand times.
			const int32 NotesBefore = Notes.Num();
			ImportValue(Array->Inner, Helper.GetRawPtr(Index), (*Items)[Index], FString::Printf(TEXT("%s[%d]"), *Context, Index));
			if (Index > 0 && Notes.Num() > NotesBefore)
			{
				Notes.SetNum(NotesBefore);
			}
		}
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Target = bIsObject ? ResolveReference(*Object, Context) : nullptr;
		if (Target != nullptr && !Target->IsA(ObjectProperty->PropertyClass))
		{
			Note(FString::Printf(TEXT("  wrong class      %s: %s is not a %s"), *Context, *Target->GetPathName(), *ObjectProperty->PropertyClass->GetName()));
			Target = nullptr;
		}
		ObjectProperty->SetObjectPropertyValue(Value, Target);
		return;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (!bIsObject)
		{
			Note(FString::Printf(TEXT("  type mismatch    %s expects an object"), *Context));
			return;
		}
		const TSharedPtr<FJsonObject>& Fields = *Object;
		const UScriptStruct* Struct = StructProperty->Struct;
		const FString Space = Property->GetMetaData(UnitySpaceMeta);

		if (Struct == TBaseStructure<FVector>::Get())
		{
			const FVector Unity(Component(Fields, TEXT("x")), Component(Fields, TEXT("y")), Component(Fields, TEXT("z")));
			*static_cast<FVector*>(Value) = Space.IsEmpty() ? Unity : FromUnityAxes(Unity);
		}
		else if (Struct == TBaseStructure<FQuat>::Get())
		{
			const FVector Axis(Component(Fields, TEXT("x")), Component(Fields, TEXT("y")), Component(Fields, TEXT("z")));
			const FVector Moved = Space.IsEmpty() ? Axis : FromUnityAxes(Axis);
			*static_cast<FQuat*>(Value) = FQuat(Moved.X, Moved.Y, Moved.Z, Component(Fields, TEXT("w")));
		}
		else if (Struct == TBaseStructure<FVector2D>::Get())
		{
			*static_cast<FVector2D*>(Value) = FVector2D(Component(Fields, TEXT("x")), Component(Fields, TEXT("y")));
		}
		else if (Struct == TBaseStructure<FVector4>::Get())
		{
			*static_cast<FVector4*>(Value) = FVector4(Component(Fields, TEXT("x")), Component(Fields, TEXT("y")), Component(Fields, TEXT("z")), Component(Fields, TEXT("w")));
		}
		else if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			*static_cast<FLinearColor*>(Value) = FLinearColor(Component(Fields, TEXT("r")), Component(Fields, TEXT("g")), Component(Fields, TEXT("b")), Component(Fields, TEXT("a")));
		}
		else if (Struct == TBaseStructure<FFloatInterval>::Get())
		{
			// A Unity [MinMaxSlider] band is a Vector2: x = min, y = max.
			*static_cast<FFloatInterval*>(Value) = FFloatInterval(Component(Fields, TEXT("x")), Component(Fields, TEXT("y")));
		}
		else
		{
			ImportStruct(Struct, Value, Fields, Context);
		}
		return;
	}

	Note(FString::Printf(TEXT("  unsupported      %s (%s)"), *Context, *Property->GetClass()->GetName()));
}

UObject* FRunnerUnityImporter::ResolveReference(const TSharedPtr<FJsonObject>& Json, const FString& Context)
{
	const TSharedPtr<FJsonObject>* Reference = nullptr;
	if (!Json->TryGetObjectField(TEXT("$ref"), Reference))
	{
		return nullptr;
	}

	FString Guid, UnityPath;
	(*Reference)->TryGetStringField(TEXT("guid"), Guid);
	(*Reference)->TryGetStringField(TEXT("path"), UnityPath);

	const FString* ObjectPath = ObjectPathByGuid.Find(Guid);
	if (ObjectPath == nullptr)
	{
		Note(FString::Printf(TEXT("  unresolved ref   %s -> %s (not ported yet)"), *Context, *UnityPath));
		return nullptr;
	}

	UObject* Target = StaticLoadObject(UObject::StaticClass(), nullptr, **ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Target == nullptr && !Options.bDryRun)
	{
		// Not on disk yet: it is later in this run. Make the empty asset now; its own turn fills it.
		const FSource* Pending = Sources.FindByPredicate([&Guid](const FSource& Source) { return Source.Guid == Guid; });
		if (Pending != nullptr)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(*ObjectPath);
			UPackage* Package = CreatePackage(*PackageName);
			Target = NewObject<UObject>(Package, Pending->Class, *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Target);
		}
	}
	if (Target == nullptr)
	{
		Note(FString::Printf(TEXT("  unresolved ref   %s -> %s (%s is not in the project; import its type too)"), *Context, *UnityPath, **ObjectPath));
	}
	return Target;
}

void FRunnerUnityImporter::Note(const FString& Line)
{
	Notes.Add(Line);
}

void FRunnerUnityImporter::WriteReport()
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Runner Unity import — %s%s"), *FDateTime::Now().ToString(), Options.bDryRun ? TEXT(" (dry run)") : TEXT("")));
	Lines.Add(FString::Printf(TEXT("Source: %s"), *DataDir));
	Lines.Add(FString::Printf(TEXT("Created %d, updated %d, skipped %d (already in the project; pass -Overwrite to re-import)."), Created, Updated, Skipped));
	Lines.Add(TEXT(""));
	Lines.Append(Notes);

	if (!UnportedTypes.IsEmpty())
	{
		UnportedTypes.Sort();
		Lines.Add(TEXT(""));
		Lines.Add(TEXT("Unity types with no UE class yet (add meta=(UnityType=\"...\") to the class that ports them):"));
		for (const FString& Type : UnportedTypes)
		{
			Lines.Add(TEXT("  ") + Type);
		}
	}

	FFileHelper::SaveStringArrayToFile(Lines, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
