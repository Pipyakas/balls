using System;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Balls.Editor
{
    /// <summary>Batchmode entry points: <c>-executeMethod Balls.Editor.BallsBuilder.BuildWindows</c>.</summary>
    public static class BallsBuilder
    {
        private const string PlayerPath = "Builds/Windows/ColorBlastBallsUnity.exe";

        [MenuItem("Balls/Build Windows Player")]
        public static void BuildWindowsFromMenu() => BuildWindows();

        public static void BuildWindows()
        {
            ProjectSetup.SetUp();

            var options = new BuildPlayerOptions
            {
                scenes = new[] { ProjectSetup.ScenePath },
                locationPathName = PlayerPath,
                target = BuildTarget.StandaloneWindows64,
                targetGroup = BuildTargetGroup.Standalone,
                options = BuildOptions.None,
            };
            BuildReport report = BuildPipeline.BuildPlayer(options);
            if (report.summary.result != BuildResult.Succeeded)
            {
                throw new Exception($"Unity build failed: {report.summary.result} ({report.summary.totalErrors} errors)");
            }
            Debug.Log($"built {PlayerPath} ({report.summary.totalSize / (1024 * 1024)} MB)");
        }
    }
}
