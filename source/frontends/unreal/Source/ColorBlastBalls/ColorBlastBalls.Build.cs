using UnrealBuildTool;

public class ColorBlastBalls : ModuleRules
{
    public ColorBlastBalls(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore", "BallsCore", "BallsRendering", "InputCore", "RenderCore", "RHI", "Slate", "SlateCore",
        });
        // The file picker behind the level Import button.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("DesktopPlatform");
        }
    }
}
