using UnrealBuildTool;

public class ColorBlastBallsTarget : TargetRules
{
    public ColorBlastBallsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("ColorBlastBalls");

        // The engine fork's Misc/Build.h defaults its UEX/XFSDK feature macros to 0, but the fork's
        // Renderer/RenderCore/Engine Build.cs files define them to 1. The shared PCH sees Build.h
        // first, so clang redefines them and -Werror turns -Wmacro-redefined into an error. MSVC
        // (the Win64 build) only warns, so match that behaviour on Android.
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            AdditionalCompilerArguments = "-Wno-macro-redefined";
        }
    }
}
