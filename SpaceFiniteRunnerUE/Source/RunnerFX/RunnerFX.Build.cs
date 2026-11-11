using UnrealBuildTool;

public class RunnerFX : ModuleRules
{
	public RunnerFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerUI", "Niagara" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}