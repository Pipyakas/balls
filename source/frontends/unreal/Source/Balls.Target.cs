using UnrealBuildTool;

public class BallsTarget : TargetRules
{
    public BallsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Balls");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            WindowsPlatform.CompilerVersion = "Latest";
        }

        if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Linux)
        {
            AdditionalCompilerArguments = "-Wno-macro-redefined -Wno-logical-op-parentheses";
        }
    }
}
