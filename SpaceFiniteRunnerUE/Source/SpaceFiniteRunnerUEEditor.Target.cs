using UnrealBuildTool;

public class SpaceFiniteRunnerUEEditorTarget : TargetRules
{
	public SpaceFiniteRunnerUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.AddRange(new string[] { "RunnerCore", "RunnerEditor" });
	}
}