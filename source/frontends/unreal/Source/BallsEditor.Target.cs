using UnrealBuildTool;

public class BallsEditorTarget : TargetRules
{
    public BallsEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
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
    }
}
