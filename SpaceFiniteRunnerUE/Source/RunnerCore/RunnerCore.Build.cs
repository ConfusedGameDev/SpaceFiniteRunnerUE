using UnrealBuildTool;

public class RunnerCore : ModuleRules
{
	public RunnerCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RunnerContracts", "RunnerSave", "RunnerUI", "RunnerFX", "RunnerCameras", "RunnerShip", "EnhancedInput" });
		// Dynamic Mesh: the streamed collision chunks (complex-as-simple).
		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryCore", "GeometryFramework" });
		// The build tests flush the Chaos query structure by hand.
		PrivateDependencyModuleNames.Add("PhysicsCore");
	}
}