using System.IO;
using Balls.App.Screens;
using Balls.Game;
using Balls.Native;
using Balls.Rendering;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// The scene's only MonoBehaviour: it primes the native plugin, hands Unity's Update and OnGUI to
    /// <see cref="BallsApp"/>, and runs the headless capture agent the verification loop uses.
    /// </summary>
    public sealed class BallsBootstrap : MonoBehaviour
    {
        [Header("Rendering")]
        [SerializeField] private Material ballMaterial;
        [SerializeField] private Material quadMaterial;
        [SerializeField] private float ballRadius = 0.2f;

        [Header("Headless verification")]
        [SerializeField] private string levelName = "DA_FirstLevel";
        [SerializeField] private bool performanceMode;
        [SerializeField] private int backendKind = BallsNative.BackendAvx2;

        private BallsApp app;
        private Camera viewCamera;

        // -balls-shot <png> captures after `captureDelay` and quits; -balls-auto starts a game and clicks launchers.
        private string screenshotPath;
        private float captureAt = -1f;
        private float quitAt = -1f;
        private bool captured;
        private bool autoPlay;
        // -balls-fill <count>: a Performance game is filled with that many balls once it runs, for frame-time runs.
        private ulong fillBalls;
        private bool filled;
        private bool autoPlayed;
        private bool parityRun;
        private bool parityDone;
        private int switchTo = -1;
        private float switchAt = 3f;
        private bool switched;
        private int controlPort;
        private float startedAt;
        private float captureDelay = 6f;

        public BallsApp App => app;

        private void Awake()
        {
            ReadCommandLine();
            RenderAssets.Set(ballMaterial, quadMaterial);
            RenderAssets.SetBallRadius(ballRadius);

            app = new BallsApp();
            app.AddScreen(new MainMenuScreen());
            app.AddScreen(new LevelSelectScreen());
            app.AddScreen(new GameplayScreen());
            app.AddScreen(new SettingsScreen());
            app.AddScreen(new EditorScreen());
            app.AddScreen(new BenchmarkScreen());
            app.AddScreen(new LoadErrorScreen());
            app.StartControlApi(controlPort);

            try
            {
                int version = BallsNative.balls_abi_version();
                if (version != BallsNative.AbiVersion)
                {
                    app.StartupError = $"balls_unity.dll speaks ABI {version}, the front end expects {BallsNative.AbiVersion}";
                    return;
                }
                app.InitializeFlow();
                Balls.Ui.Palette.LoadFromCore();
                RenderPrimitives.SetPalette(RenderAssets.BallMaterial());
                BallsNative.balls_set_plugin_directory(PluginDirectory());
                app.LoadSettings();
                app.ApplyOrientation();
                app.ApplyLaunchOptions(System.Environment.GetCommandLineArgs());
                Debug.Log($"balls: plugin directory {PluginDirectory()}, ABI {version}");
            }
            catch (System.Exception error)
            {
                app.StartupError = $"cannot load balls_unity.dll: {error.Message}";
                Debug.LogException(error);
            }

            BallsApp.HoldingForCapture = screenshotPath != null;
            startedAt = Time.realtimeSinceStartup;
            viewCamera = RenderPrimitives.CreateCamera(7, 14, 3);
            app.Camera = viewCamera;
        }

        private void OnDestroy()
        {
            app?.DestroyFlow();
        }

        private void Update()
        {
            RunCaptureAgent();
            app?.Update();
        }

        private void OnGUI()
        {
            if (app == null)
            {
                return;
            }
            if (!string.IsNullOrEmpty(app.StartupError))
            {
                var style = new GUIStyle(GUI.skin.label) { fontSize = 18 };
                GUILayout.Label($"<color=#ff7070>{app.StartupError}</color>", style);
                return;
            }
            app.OnGUI();
        }

        private void ReadCommandLine()
        {
            string[] args = System.Environment.GetCommandLineArgs();
            for (int index = 0; index < args.Length; ++index)
            {
                switch (args[index])
                {
                    case "-balls-shot" when index + 1 < args.Length:
                        screenshotPath = args[index + 1];
                        captureAt = Time.realtimeSinceStartup + captureDelay;
                        quitAt = captureAt + 2f;
                        break;
                    case "-balls-shot-delay" when index + 1 < args.Length:
                        if (float.TryParse(args[index + 1], out float seconds))
                        {
                            captureDelay = Mathf.Max(0.5f, seconds);
                        }
                        break;
                    case "-balls-auto":
                        autoPlay = true;
                        break;
                    case "-balls-fill" when index + 1 < args.Length:
                        ulong.TryParse(args[index + 1], out fillBalls);
                        break;
                    case "-balls-physics-thread":
                        Balls.Game.NativeGameBackend.UsePhysicsThread = true;
                        break;
                    case "-balls-parity":
                        parityRun = true;
                        break;
                    case "-balls-control" when index + 1 < args.Length:
                        if (int.TryParse(args[index + 1], out int port))
                        {
                            controlPort = port;
                        }
                        break;
                    case "-BallsGpu" when index + 1 < args.Length:
                        if (!BallsNative.SetComputeGpu(args[index + 1]))
                        {
                            Debug.LogWarning($"balls: no compute GPU matches '{args[index + 1]}'");
                        }
                        break;
                    case "-balls-switch" when index + 1 < args.Length:
                        if (BallsNative.TryBackendKind(args[index + 1], out int kind))
                        {
                            switchTo = kind;
                        }
                        else
                        {
                            Debug.LogWarning($"balls: unknown backend '{args[index + 1]}' for -balls-switch");
                        }
                        break;
                    case "-balls-switch-delay" when index + 1 < args.Length:
                        if (float.TryParse(args[index + 1], out float delay))
                        {
                            switchAt = Mathf.Max(0.1f, delay);
                        }
                        break;
                }
            }
        }

        private void RunCaptureAgent()
        {
            float now = Time.realtimeSinceStartup;
            if (parityRun && !parityDone && app != null && now - startedAt > 0.5f)
            {
                parityDone = true;
                string levelDir = app.LevelDirectoryFor(BallsApp.NormalMode);
                ParityProbe.RunControlledSuite(levelDir, "DA_FirstLevel");
                var result = ParityProbe.Run(levelDir, "DA_FirstLevel", 200, 300);
                Debug.Log($"balls: parity compared={result.compared} mismatches={result.mismatches} " +
                          $"worstDelta={result.worstDelta} first={result.firstMismatch} " +
                          $"sorted={result.sortedMismatches} firstDivergentTick={result.firstDivergentTick} " +
                          $"countMismatches={result.countMismatches} colorMismatches={result.colorMismatches} " +
                          $"firstBehavioralTick={result.firstBehavioralTick} firstBehavioralBall={result.firstBehavioralBall} " +
                          $"firstBehavioralDelta={result.firstBehavioralDelta} behavioralReason='{result.firstBehavioralReason}' " +
                          $"divergenceReason='{result.firstDivergentReason}' error='{result.error}'");
                Application.Quit();
                return;
            }
            if (autoPlay && !autoPlayed && now - startedAt > 1.5f)
            {
                autoPlayed = true;
                if (!app.Running)
                {
                    int mode = performanceMode ? BallsApp.PerformanceMode : BallsApp.NormalMode;
                    BallsNative.balls_flow_start_game(app.FlowHandle, mode, levelName);
                }
                if (app.Mode == BallsApp.PerformanceMode)
                {
                    BallsInput.RequestAutoRelease();
                }
                else
                {
                    BallsInput.ClickLauncher(0, 0);
                    BallsInput.ClickLauncher(1, 0);
                    BallsInput.ClickLauncher(2, 0);
                }
            }
            if (fillBalls > 0 && !filled && app != null && app.Running && app.Mode == BallsApp.PerformanceMode &&
                SessionSystem.Current?.Backend is Balls.Game.NativeGameBackend native)
            {
                filled = true;
                BallsNative.balls_session_fill_with_balls(native.SessionHandle, fillBalls);
            }
            if (switchTo >= 0 && !switched && app != null && app.Running && now - startedAt > switchAt)
            {
                switched = true;
                app.SwitchBackend(switchTo);
            }
            if (screenshotPath == null)
            {
                return;
            }
            if (!captured && now >= captureAt)
            {
                captured = true;
                ScreenCapture.CaptureScreenshot(screenshotPath);
                Debug.Log($"balls: capturing to {screenshotPath} at {now:F2}s, error='{app?.StartupError}'");
            }
            if (quitAt > 0f && now >= quitAt)
            {
                Application.Quit();
            }
        }

        private static string PluginDirectory()
        {
            string besideData = Path.Combine(Application.dataPath, "Plugins", "x86_64");
            return Directory.Exists(besideData) ? besideData : System.AppDomain.CurrentDomain.BaseDirectory;
        }
    }
}
