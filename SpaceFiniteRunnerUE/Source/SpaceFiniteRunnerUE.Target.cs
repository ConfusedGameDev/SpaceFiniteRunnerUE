using UnrealBuildTool;

public class SpaceFiniteRunnerUETarget : TargetRules
{
	public SpaceFiniteRunnerUETarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.Add("RunnerCore");
	}
}