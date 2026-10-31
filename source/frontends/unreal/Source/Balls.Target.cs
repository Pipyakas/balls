using UnrealBuildTool;

public class BallsTarget : TargetRules
{
    public BallsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Balls");

        // BuildCore.bat builds balls_core.lib with CMake's default, the newest Visual Studio installed. Unreal 5.8.4
        // otherwise prefers its own tested MSVC (14.44), and a core compiled by a newer STL then fails to link
        // (__std_min_element_8u and other vectorized helpers 14.44's runtime lacks). A newer linker takes the stock
        // engine's 14.44 libraries fine, so build the game with the newest compiler too.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            WindowsPlatform.CompilerVersion = "Latest";
        }

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
