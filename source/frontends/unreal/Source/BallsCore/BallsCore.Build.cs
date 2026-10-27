using System.IO;
using UnrealBuildTool;

// Links the engine-agnostic core (repo/core) built by BuildCore.bat as balls_core.lib.
public class BallsCore : ModuleRules
{
    public BallsCore(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", ".."));
        PublicSystemIncludePaths.Add(Path.Combine(RepoRoot, "source", "core"));
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "android-ue", "source", "core", "libballs_core.a"));
        }
        else
        {
            PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "win32-ue", "source", "core", "Release", "balls_core.lib"));

            // The physics backends (CPU, CUDA) are DLLs the core loads at run time. Packaged builds stage them next
            // to the executable; the editor finds them through BALLS_PLUGIN_DIR (see FBallsApp).
            string PluginDirectory = Path.Combine(RepoRoot, "build", "win32-ue", "bin", "Release");
            PublicDefinitions.Add("BALLS_PLUGIN_DIR=\"" + PluginDirectory.Replace('\\', '/') + "\"");
            foreach (string Plugin in new[] { "balls_physics_cpu.dll", "balls_physics_cuda.dll" })
            {
                string PluginPath = Path.Combine(PluginDirectory, Plugin);
                if (File.Exists(PluginPath))
                {
                    RuntimeDependencies.Add("$(BinaryOutputDir)/" + Plugin, PluginPath);
                }
            }
        }
        PublicDefinitions.Add("BALLS_ASSET_DIR=\"" + Path.Combine(RepoRoot, "data", "assets").Replace('\\', '/') + "\"");
    }
}
