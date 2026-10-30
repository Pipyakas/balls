using System;
using System.IO;
using Balls.Native;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Balls.Editor
{
    /// <summary>
    /// Batchmode entry points: <c>-executeMethod Balls.Editor.BallsBuilder.BuildWindows</c>,
    /// <c>-executeMethod Balls.Editor.BallsBuilder.BuildLinux</c>, and
    /// <c>-executeMethod Balls.Editor.BallsBuilder.RunParity</c>.
    /// </summary>
    public static class BallsBuilder
    {
        private const string WindowsPlayerPath = "Builds/Windows/ColorBlastBallsUnity.exe";
        private const string LinuxPlayerPath = "Builds/Linux/ColorBlastBallsUnity.x86_64";

        [MenuItem("Balls/Build Windows Player")]
        public static void BuildWindowsFromMenu() => BuildWindows();

        [MenuItem("Balls/Build Linux Player")]
        public static void BuildLinuxFromMenu() => BuildLinux();

        [MenuItem("Balls/Run Parity Probe")]
        public static void RunParity()
        {
            string pluginDir = Path.GetFullPath(Path.Combine(Application.dataPath, "Plugins", "x86_64"));
            BallsNative.balls_set_plugin_directory(pluginDir);
            string levelDir = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "..", "data", "assets", "levels", "normal"));
            App.ParityProbe.RunControlledSuite(levelDir, "DA_FirstLevel");
            var result = App.ParityProbe.Run(levelDir, "DA_FirstLevel", 200, 300);
            Debug.Log($"balls: parity compared={result.compared} mismatches={result.mismatches} " +
                      $"worstDelta={result.worstDelta} first={result.firstMismatch} " +
                      $"sorted={result.sortedMismatches} firstDivergentTick={result.firstDivergentTick} " +
                      $"countMismatches={result.countMismatches} colorMismatches={result.colorMismatches} " +
                      $"firstBehavioralTick={result.firstBehavioralTick} firstBehavioralBall={result.firstBehavioralBall} " +
                      $"firstBehavioralDelta={result.firstBehavioralDelta} behavioralReason='{result.firstBehavioralReason}' " +
                      $"divergenceReason='{result.firstDivergentReason}' error='{result.error}'");
        }

        public static void BuildWindows() => Build(BuildTarget.StandaloneWindows64, PlayerPath(WindowsPlayerPath), "Windows");

        public static void BuildLinux() => Build(BuildTarget.StandaloneLinux64, PlayerPath(LinuxPlayerPath), "Linux");

        /// <summary>
        /// <c>-balls-player-path &lt;file&gt;</c> lets source/frontends/unity/build.bat put the player in its own
        /// staging folder under build\, like the other front ends; the editor menu keeps the Builds/ default.
        /// </summary>
        private static string PlayerPath(string fallback)
        {
            string[] args = Environment.GetCommandLineArgs();
            for (int index = 0; index + 1 < args.Length; ++index)
            {
                if (args[index] == "-balls-player-path")
                {
                    return args[index + 1];
                }
            }
            return fallback;
        }

        private static void Build(BuildTarget target, string playerPath, string label)
        {
            if (!File.Exists(ProjectSetup.ScenePath))
            {
                ProjectSetup.SetUp();
            }
            else
            {
                ProjectSetup.CopyNativePlugins();
                ProjectSetup.CopyLevels();
                ProjectSetup.CopyFonts();
            }

            // The client machines are Linux, so the player is IL2CPP there; Windows keeps Mono for fast iteration.
            // Standalone Windows and Linux share one backend setting, so it is put back afterwards: otherwise every
            // Windows build would leave ProjectSettings.asset modified.
            var namedTarget = NamedBuildTarget.FromBuildTargetGroup(BuildPipeline.GetBuildTargetGroup(target));
            ScriptingImplementation savedBackend = PlayerSettings.GetScriptingBackend(namedTarget);
            PlayerSettings.SetScriptingBackend(namedTarget,
                target == BuildTarget.StandaloneLinux64 ? ScriptingImplementation.IL2CPP : ScriptingImplementation.Mono2x);

            // The debug gadget's GPU ms row reads Unity's frame timing, which is off unless a build asks for it.
            PlayerSettings.enableFrameTimingStats = true;

            var options = new BuildPlayerOptions
            {
                scenes = new[] { ProjectSetup.ScenePath },
                locationPathName = playerPath,
                target = target,
                targetGroup = BuildPipeline.GetBuildTargetGroup(target),
                options = BuildOptions.None,
            };
            BuildReport report;
            try
            {
                report = BuildPipeline.BuildPlayer(options);
            }
            finally
            {
                PlayerSettings.SetScriptingBackend(namedTarget, savedBackend);
                AssetDatabase.SaveAssets();
            }
            if (report.summary.result != BuildResult.Succeeded)
            {
                throw new Exception($"Unity {label} build failed: {report.summary.result} ({report.summary.totalErrors} errors)");
            }
            Debug.Log($"built {playerPath} ({report.summary.totalSize / (1024 * 1024)} MB)");
        }
    }
}
