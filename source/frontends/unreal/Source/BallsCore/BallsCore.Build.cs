using System.IO;
using UnrealBuildTool;

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
