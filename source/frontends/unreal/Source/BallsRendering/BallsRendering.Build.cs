using UnrealBuildTool;

public class BallsRendering : ModuleRules
{
    public BallsRendering(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "RenderCore", "Renderer", "RHI" });
    }
}
