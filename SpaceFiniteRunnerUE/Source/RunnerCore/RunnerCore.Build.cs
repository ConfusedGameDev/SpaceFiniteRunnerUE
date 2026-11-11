using UnrealBuildTool;

public class RunnerCore : ModuleRules
{
	public RunnerCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerContracts", "RunnerSave", "RunnerUI", "RunnerFX", "RunnerCameras", "RunnerShip", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}