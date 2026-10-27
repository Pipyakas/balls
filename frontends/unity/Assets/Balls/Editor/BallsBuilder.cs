using System;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Balls.Editor
{
    /// <summary>
    /// Batchmode entry points: <c>-executeMethod Balls.Editor.BallsBuilder.BuildWindows</c> and
    /// <c>-executeMethod Balls.Editor.BallsBuilder.BuildLinux</c>.
    /// </summary>
    public static class BallsBuilder
    {
        private const string WindowsPlayerPath = "Builds/Windows/ColorBlastBallsUnity.exe";
        private const string LinuxPlayerPath = "Builds/Linux/ColorBlastBallsUnity.x86_64";

        [MenuItem("Balls/Build Windows Player")]
        public static void BuildWindowsFromMenu() => BuildWindows();

        [MenuItem("Balls/Build Linux Player")]
        public static void BuildLinuxFromMenu() => BuildLinux();

        public static void BuildWindows() => Build(BuildTarget.StandaloneWindows64, WindowsPlayerPath, "Windows");

        public static void BuildLinux() => Build(BuildTarget.StandaloneLinux64, LinuxPlayerPath, "Linux");

        private static void Build(BuildTarget target, string playerPath, string label)
        {
            ProjectSetup.SetUp();
            // The client machines are Linux, so the player is IL2CPP there; Windows keeps Mono for fast iteration.
            PlayerSettings.SetScriptingBackend(NamedBuildTarget.FromBuildTargetGroup(BuildPipeline.GetBuildTargetGroup(target)),
                target == BuildTarget.StandaloneLinux64 ? ScriptingImplementation.IL2CPP : ScriptingImplementation.Mono2x);

            var options = new BuildPlayerOptions
            {
                scenes = new[] { ProjectSetup.ScenePath },
                locationPathName = playerPath,
                target = target,
                targetGroup = BuildPipeline.GetBuildTargetGroup(target),
                options = BuildOptions.None,
            };
            BuildReport report = BuildPipeline.BuildPlayer(options);
            if (report.summary.result != BuildResult.Succeeded)
            {
                throw new Exception($"Unity {label} build failed: {report.summary.result} ({report.summary.totalErrors} errors)");
            }
            Debug.Log($"built {playerPath} ({report.summary.totalSize / (1024 * 1024)} MB)");
        }
    }
}
