using UnrealBuildTool;

public class BallsEditorTarget : TargetRules
{
    public BallsEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Balls");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            WindowsPlatform.CompilerVersion = "Latest";
        }
    }
}
