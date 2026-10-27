using System.IO;
using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using Balls.Rendering;
using Unity.Entities;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// The scene's only MonoBehaviour: it primes the native plugin, frames the camera, turns Unity input into
    /// requests for the session system and draws the debug overlay. Everything else is ECS.
    /// </summary>
    public sealed class BallsBootstrap : MonoBehaviour
    {
        [Header("Level")]
        [SerializeField] private string levelDirectoryOverride = "";
        [SerializeField] private string levelName = "DA_FirstLevel";
        [SerializeField] private bool performanceMode;

        [Header("Physics")]
        [SerializeField] private int backendKind = BallsNative.BackendAvx2;

        [Header("Rendering")]
        [SerializeField] private Material ballMaterial;
        [SerializeField] private Material quadMaterial;
        [SerializeField] private float ballRadius = 0.2f;

        private Camera viewCamera;
        private bool started;
        private string startupError = "";
        private BoardFrame lastFrame;
        private bool cameraFramed;
        private float fpsSmoothed;

        // Headless verification: -balls-shot <png> captures the frame and quits, -balls-auto clicks launchers.
        private string screenshotPath;
        private float captureAt = -1f;
        private float quitAt = -1f;
        private bool captured;
        private bool autoPlayPending;

        public string LevelDirectory => string.IsNullOrEmpty(levelDirectoryOverride) ? DefaultLevelDirectory() : levelDirectoryOverride;

        private void Awake()
        {
            ReadCommandLine();
            RenderAssets.Set(ballMaterial, quadMaterial);
            RenderAssets.SetBallRadius(ballRadius);
            RenderPrimitives.SetPalette(RenderAssets.BallMaterial());

            try
            {
                int version = BallsNative.balls_abi_version();
                if (version != BallsNative.AbiVersion)
                {
                    startupError = $"balls_unity.dll speaks ABI {version}, the front end expects {BallsNative.AbiVersion}";
                    return;
                }
                BallsNative.balls_set_plugin_directory(PluginDirectory());
                BallsInput.RequestStart(new BallsInput.StartRequest
                {
                    mode = performanceMode ? BallsNative.ModePerformance : BallsNative.ModeNormal,
                    levelDirectory = LevelDirectory,
                    levelName = levelName,
                    backend = backendKind,
                });
                started = true;
            }
            catch (System.Exception error)
            {
                startupError = $"cannot load balls_unity.dll: {error.Message}";
                Debug.LogException(error);
            }
        }

        private void Update()
        {
            if (!started)
            {
                return;
            }
            fpsSmoothed = Mathf.Lerp(fpsSmoothed <= 0f ? 1f / Mathf.Max(Time.unscaledDeltaTime, 1e-4f) : fpsSmoothed,
                1f / Mathf.Max(Time.unscaledDeltaTime, 1e-4f), 0.1f);

            RunCaptureAgent();

            // Keys follow the other front ends: R restarts, Space starts the performance release, F1/F2 pick a backend.
            if (Input.GetKeyDown(KeyCode.R))
            {
                BallsInput.RequestRestart();
            }
            if (Input.GetKeyDown(KeyCode.Space))
            {
                BallsInput.RequestAutoRelease();
            }
            if (Input.GetKeyDown(KeyCode.F1))
            {
                BallsInput.RequestBackendSwitch(BallsNative.BackendScalar);
            }
            if (Input.GetKeyDown(KeyCode.F2))
            {
                BallsInput.RequestBackendSwitch(BallsNative.BackendAvx2);
            }
            if (Input.GetKeyDown(KeyCode.Escape))
            {
                Application.Quit();
            }

            World world = World.DefaultGameObjectInjectionWorld;
            if (world == null || !TryGetBoard(world.EntityManager, out Entity board))
            {
                return;
            }
            var em = world.EntityManager;
            var info = em.GetComponentData<BoardInfoData>(board);
            var session = em.GetComponentData<SessionInfo>(board);
            if (!info.valid)
            {
                return;
            }

            lastFrame = new BoardFrame
            {
                columns = info.columns,
                rows = info.rows,
                playableHeight = info.playableHeight,
                paddedWidth = info.paddedWidth,
            };

            if (viewCamera == null)
            {
                viewCamera = RenderPrimitives.CreateCamera(info.columns, info.rows, LauncherRows(em, board));
            }
            if (!cameraFramed)
            {
                RenderPrimitives.CreateCamera(info.columns, info.rows, LauncherRows(em, board));
                cameraFramed = true;
            }

            if (Input.GetMouseButtonDown(0))
            {
                HandleClick(info, session);
            }
        }

        private void HandleClick(BoardInfoData info, SessionInfo session)
        {
            Camera camera = viewCamera != null ? viewCamera : Camera.main;
            if (camera == null)
            {
                return;
            }
            Vector3 worldPosition = camera.ScreenToWorldPoint(Input.mousePosition);
            int column = Mathf.FloorToInt(worldPosition.x);
            float launcherBase = info.rows + 0.9f;
            int row = Mathf.RoundToInt((worldPosition.y - launcherBase) / 1.15f);
            if (column < 0 || column >= info.columns || row < 0)
            {
                return;
            }
            BallsInput.ClickLauncher(column, row);
        }

        private static bool TryGetBoard(EntityManager em, out Entity board)
        {
            board = Entity.Null;
            using var query = em.CreateEntityQuery(typeof(BoardInfoData));
            if (query.IsEmptyIgnoreFilter)
            {
                return false;
            }
            board = query.GetSingletonEntity();
            return true;
        }

        private static int LauncherRows(EntityManager em, Entity board)
        {
            var launchers = em.GetBuffer<LauncherElement>(board, true);
            int rows = 0;
            for (int index = 0; index < launchers.Length; ++index)
            {
                if (launchers[index].present)
                {
                    rows = Mathf.Max(rows, launchers[index].row + 1);
                }
            }
            return Mathf.Max(rows, 1);
        }

        private void OnGUI()
        {
            var style = new GUIStyle(GUI.skin.label) { fontSize = 14, richText = true };
            GUILayout.BeginArea(new Rect(12f, 10f, 520f, 260f));
            GUILayout.Label("<b>Balls — Unity ECS front end</b>", style);
            if (!string.IsNullOrEmpty(startupError))
            {
                GUILayout.Label($"<color=#ff7070>{startupError}</color>", style);
                GUILayout.EndArea();
                return;
            }
            GUILayout.Label($"level: {levelName}  fps: {fpsSmoothed:F0}", style);
            if (lastFrame.columns > 0)
            {
                GUILayout.Label($"board: {lastFrame.columns}x{lastFrame.rows}", style);
            }
            World world = World.DefaultGameObjectInjectionWorld;
            if (world != null && TryGetBoard(world.EntityManager, out Entity board))
            {
                var em = world.EntityManager;
                var session = em.GetComponentData<SessionInfo>(board);
                var batch = em.GetComponentData<BallBatch>(board);
                GUILayout.Label($"backend: {BallsNative.BackendName(session.backend)}  state: {StateName(session.state)}", style);
                GUILayout.Label($"balls: {batch.count:N0}  launchers left: {session.launcherRemaining}", style);
            }
            GUILayout.Label("click a launcher to release · R restart · Space auto release · F1 scalar · F2 AVX2", style);
            GUILayout.EndArea();
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
                        captureAt = Time.realtimeSinceStartup + 4f;
                        quitAt = captureAt + 2f;
                        break;
                    case "-balls-auto":
                        autoPlayPending = true;
                        break;
                }
            }
        }

        private void RunCaptureAgent()
        {
            if (autoPlayPending && Time.realtimeSinceStartup > 1.5f)
            {
                autoPlayPending = false;
                BallsInput.ClickLauncher(0, 0);
                BallsInput.ClickLauncher(1, 0);
                BallsInput.ClickLauncher(2, 0);
            }
            if (screenshotPath == null)
            {
                return;
            }
            float now = Time.realtimeSinceStartup;
            if (!captured && now >= captureAt)
            {
                captured = true;
                ScreenCapture.CaptureScreenshot(screenshotPath);
                Debug.Log($"captured {screenshotPath}");
            }
            if (quitAt > 0f && now >= quitAt)
            {
                Application.Quit();
            }
        }

        private static string StateName(int state) => state switch
        {
            BallsNative.StateRunning => "running",
            BallsNative.StateWon => "won",
            _ => "ready",
        };

        private static string PluginDirectory()
        {
            string besideData = Path.Combine(Application.dataPath, "Plugins", "x86_64");
            return Directory.Exists(besideData) ? besideData : System.AppDomain.CurrentDomain.BaseDirectory;
        }

        private static string DefaultLevelDirectory()
        {
#if UNITY_EDITOR
            string projectRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", ".."));
            return Path.Combine(projectRoot, "data", "assets", "levels", "normal");
#else
            return Path.Combine(Application.streamingAssetsPath, "levels", "normal");
#endif
        }
    }
}
