using UnrealBuildTool;

public class RunnerUI : ModuleRules
{
	public RunnerUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerSave", "UMG", "CommonUI", "CommonInput", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "MediaAssets" });
	}
}