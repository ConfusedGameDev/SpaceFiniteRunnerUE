using UnrealBuildTool;

public class RunnerShip : ModuleRules
{
	public RunnerShip(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerUI", "RunnerCameras", "RunnerContracts", "EnhancedInput", "PhysicsCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}