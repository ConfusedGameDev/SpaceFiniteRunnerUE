using UnrealBuildTool;

public class RunnerEditor : ModuleRules
{
	public RunnerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "Json", "JsonUtilities", "AssetTools", "AssetRegistry", "Slate", "SlateCore" });
	}
}