using System.IO;
using UnrealBuildTool;

// Links the engine-agnostic core (repo/core) built by BuildCore.bat as balls_core.lib.
public class BallsCore : ModuleRules
{
    public BallsCore(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));
        PublicSystemIncludePaths.Add(Path.Combine(RepoRoot, "core"));
        PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build-unreal", "core", "Release", "balls_core.lib"));
        PublicDefinitions.Add("BALLS_ASSET_DIR=\"" + Path.Combine(RepoRoot, "assets").Replace('\\', '/') + "\"");
    }
}
