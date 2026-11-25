using UnrealBuildTool;

public class MissionRecallEditorTarget : TargetRules
{
    public MissionRecallEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("MissionRecall");
    }
}

