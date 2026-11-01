using UnrealBuildTool;

public class Balls : ModuleRules
{
    public Balls(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore", "BallsCore", "BallsRendering", "InputCore", "RenderCore", "RHI", "Slate", "SlateCore",
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("DesktopPlatform");
        }

        if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Mac ||
            Target.Platform == UnrealTargetPlatform.Linux)
        {
            PrivateDependencyModuleNames.AddRange(new[] { "HTTPServer", "Sockets" });
        }
    }
}
