using UnrealBuildTool;

public class BallsTarget : TargetRules
{
    public BallsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Balls");

        // The engine fork's Misc/Build.h defaults its UEX/XFSDK feature macros to 0, but the fork's
        // Renderer/RenderCore/Engine Build.cs files define them to 1. The shared PCH sees Build.h
        // first, so clang redefines them and -Werror turns -Wmacro-redefined into an error. MSVC
        // (the Win64 build) only warns, so match that behaviour on Android and Linux.
        if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Linux)
        {
            AdditionalCompilerArguments = "-Wno-macro-redefined -Wno-logical-op-parentheses";
        }
    }
}
