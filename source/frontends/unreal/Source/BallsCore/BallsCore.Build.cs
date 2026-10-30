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
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            // Cross-compiled with Unreal's own Linux toolchain by BuildCoreLinux.bat, so core, plugins and the game
            // share one libc++. The physics plugins are .so files the core dlopens from next to the executable.
            PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "linux-ue", "source", "core", "libballs_core.a"));
            string PluginDirectory = Path.Combine(RepoRoot, "build", "linux-ue", "bin");
            PublicDefinitions.Add("BALLS_PLUGIN_DIR=\"" + PluginDirectory.Replace('\\', '/') + "\"");
            foreach (string Plugin in new[] { "libballs_physics_cpu.so", "libballs_physics_vulkan.so" })
            {
                string PluginPath = Path.Combine(PluginDirectory, Plugin);
                if (File.Exists(PluginPath))
                {
                    RuntimeDependencies.Add("$(BinaryOutputDir)/" + Plugin, PluginPath);
                }
            }
        }
        else
        {
            PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "win32-ue", "source", "core", "Release", "balls_core.lib"));
            // BuildCore.bat builds the Win64 core in the scoring profile unless BALLS_PROFILE=extended; the core's
            // headers change with BALLS_FIXED_TARGET, so the game module must agree with the library it links.
            if (System.Environment.GetEnvironmentVariable("BALLS_PROFILE") != "extended")
            {
                PublicDefinitions.Add("BALLS_FIXED_TARGET=1");
            }

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
