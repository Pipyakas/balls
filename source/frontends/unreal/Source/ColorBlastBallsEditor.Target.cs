using UnrealBuildTool;

public class ColorBlastBallsEditorTarget : TargetRules
{
    public ColorBlastBallsEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("ColorBlastBalls");
    }
}
