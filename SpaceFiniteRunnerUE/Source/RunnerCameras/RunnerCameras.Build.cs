using UnrealBuildTool;

public class RunnerCameras : ModuleRules
{
	public RunnerCameras(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerUI", "RunnerFX" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}