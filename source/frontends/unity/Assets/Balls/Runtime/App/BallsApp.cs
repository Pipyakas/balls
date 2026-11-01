using System;
using System.Collections.Generic;
using System.IO;
using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using Balls.Ui;
using Unity.Entities;
using UnityEngine;

namespace Balls.App
{

    public sealed class BallsApp
    {
        public const int NormalMode = BallsNative.ModeNormal;
        public const int PerformanceMode = BallsNative.ModePerformance;

        public static BallsApp Current { get; private set; }

        private readonly Dictionary<Type, Screen> screens = new();
        private Screen current;
        private IntPtr flow = IntPtr.Zero;
        private NativeFlowState flowState;

        public BallsApp()
        {
            Current = this;
            Theme = new UiTheme();
            Widgets = new Widgets(Theme);
        }

        public UiTheme Theme { get; }

        public Widgets Widgets { get; }

        public Camera Camera { get; set; }

        public int Mode { get; private set; } = NormalMode;

        public string LevelDirectory { get; private set; } = "";

        public string LevelName { get; private set; } = "";

        public int BackendKind { get; private set; } = BallsNative.BackendAvx2;

        public SettingsData Settings { get; private set; }

        public MachineProfile Profile { get; private set; } = new();

        public IntPtr FlowHandle => flow;

        public NativeFlowState FlowState => flowState;

        public int FlowScreen => flowState.screen;

        public int FlowOverlay => flowState.overlay;

        public int FlowMode => flowState.mode;

        public int FlowDiskState => flowState.diskState;

        public bool IsPaused => flowState.paused != 0;

        public int FlowButtonCount => flowState.buttonCount;

        public int FlowLevelRowCount => flowState.levelCount;

        public float FlowToastSeconds => flowState.toastSecondsLeft;

        public int FlowToastIsError => flowState.toastIsError;

        public int DefaultBackendKind => Settings != null ? Settings.BackendKind(FallbackBackend()) : FallbackBackend();

        private static int FallbackBackend()
        {
            if (BallsNative.BackendAvailable(BallsNative.BackendHybrid))
            {
                return BallsNative.BackendHybrid;
            }
            string winner = MachineProfile.TargetWinner();
            if (winner != null)
            {
                for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
                {
                    if (BallsNative.BackendName(kind).Equals(winner, StringComparison.OrdinalIgnoreCase) &&
                        BallsNative.BackendAvailable(kind))
                    {
                        return kind;
                    }
                }
            }
            return BallsNative.BackendAvailable(BallsNative.BackendAvx2) ? BallsNative.BackendAvx2 : BallsNative.BackendScalar;
        }

        public string StartupError { get; set; } = "";

        public static bool HoldingForCapture { get; set; }

        public bool Running => current is Screens.GameplayScreen;

        public ControlApi Control { get; private set; }

        public string ScreenName => flow != IntPtr.Zero
            ? BallsNative.FlowText(flow, BallsNative.FlowTextScreenName)
            : (current != null ? current.GetType().Name : "menu");

        public string ToastText => flow != IntPtr.Zero && flowState.toastSecondsLeft > 0f
            ? BallsNative.FlowText(flow, BallsNative.FlowTextToast)
            : string.Empty;

        public static string RendererText =>
            rendererText ??= $"Unity URP / {SystemInfo.graphicsDeviceType} / {SystemInfo.graphicsDeviceName}";

        private static string rendererText;

        public void InitializeFlow()
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_destroy(flow);
                flow = IntPtr.Zero;
            }
            string normal = LevelDirectoryFor(NormalMode);
            string perf = LevelDirectoryFor(PerformanceMode);
            flow = BallsNative.balls_flow_create(normal, perf);
            BallsNative.balls_flow_state(flow, out flowState);
            SyncScreenToFlow();
        }

        public void DestroyFlow()
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_destroy(flow);
                flow = IntPtr.Zero;
            }
            Control?.Dispose();
            Control = null;
        }

        public void ActivateFlow(int action, int row = -1)
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_activate(flow, action, row);
            }
        }

        public void EscapeFlow()
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_escape(flow);
            }
        }

        public void PlayFromEditor(int mode, string levelName)
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_play_from_editor(flow, mode, levelName);
            }
        }

        public void FlowImport(IReadOnlyList<string> picked)
        {
            if (flow != IntPtr.Zero && picked != null)
            {
                BallsNative.FlowImportFiles(flow, picked);
            }
        }

        public void StartControlApi(int port)
        {
            if (port <= 0 || Control != null)
            {
                return;
            }
            Control = new ControlApi(this, port);
            Control.Start();
        }

        public double GpuMs { get; private set; }

        private readonly FrameTiming[] frameTimings = new FrameTiming[1];

        private void SampleGpuTime()
        {
            FrameTimingManager.CaptureFrameTimings();
            if (FrameTimingManager.GetLatestTimings(1, frameTimings) == 1 && frameTimings[0].gpuFrameTime > 0.0)
            {
                GpuMs = GpuMs <= 0.0 ? frameTimings[0].gpuFrameTime : GpuMs * 0.9 + frameTimings[0].gpuFrameTime * 0.1;
            }
        }

        public void AddScreen<T>(T screen) where T : Screen
        {
            screen.Attach(this);
            screens[typeof(T)] = screen;
        }

        public T Get<T>() where T : Screen => (T)screens[typeof(T)];

        public void Toast(string text, bool error = false)
        {
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_show_toast(flow, text, error ? 1 : 0);
            }
        }

        public void Update()
        {
            SampleGpuTime();
            Control?.Pump();

            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_update(flow, Time.unscaledDeltaTime);
                if (Running && TryReadSession(out _, out SessionInfo sessionInfo, out _))
                {
                    BallsNative.balls_flow_session_state(flow, sessionInfo.state);
                }
                ProcessFlowRequests();
                BallsNative.balls_flow_state(flow, out flowState);
                SyncScreenToFlow();
            }

            if (Input.GetKeyDown(KeyCode.Escape))
            {
                if (current is Screens.EditorScreen editor)
                {
                    editor.HandleEscape();
                }
                else if (flow != IntPtr.Zero)
                {
                    BallsNative.balls_flow_escape(flow);
                }
            }

            if (SessionSystem.Current?.Notice is string notice)
            {
                SessionSystem.Current.Notice = null;
                Toast(notice);
            }
        }

        private void ProcessFlowRequests()
        {
            while (flow != IntPtr.Zero && BallsNative.balls_flow_take_request(flow, out NativeFlowRequest request) != 0)
            {
                switch (request.kind)
                {
                    case BallsNative.RequestStartGame:
                    case BallsNative.RequestReloadGame:
                    {
                        string levelName = request.level.Name;
                        int mode = request.mode;
                        int backendKind = BackendKind;
                        bool collisions = Settings != null && Settings.BallCollisions;
                        Mode = mode;
                        LevelName = levelName;
                        LevelDirectory = LevelDirectoryFor(mode);
                        var startReq = new BallsInput.StartRequest
                        {
                            mode = mode,
                            levelDirectory = LevelDirectory,
                            levelName = levelName,
                            backend = backendKind,
                            collisions = collisions,
                        };
                        if (SessionSystem.Current != null)
                        {
                            if (SessionSystem.Current.StartSession(in startReq, out string error))
                            {
                                BallsNative.balls_flow_level_loaded(flow);
                                Get<Screens.GameplayScreen>().BeginSession();
                            }
                            else
                            {
                                BallsNative.balls_flow_level_failed(flow, error ?? "Failed to load level");
                            }
                        }
                        break;
                    }
                    case BallsNative.RequestRestartGame:
                    {
                        SessionSystem.Current?.RestartSession();
                        Get<Screens.GameplayScreen>().OnRestarted();
                        break;
                    }
                    case BallsNative.RequestEndGame:
                    {
                        SessionSystem.Current?.EndSession();
                        break;
                    }
                    case BallsNative.RequestOpenEditor:
                    {
                        var editor = Get<Screens.EditorScreen>();
                        if (request.hasLevel != 0 && !string.IsNullOrEmpty(request.level.Name))
                        {
                            editor.StartEditing(request.mode, request.level.Name);
                        }
                        else
                        {
                            editor.StartNewLevel(request.mode);
                        }
                        break;
                    }
                    case BallsNative.RequestStartBenchmark:
                    {
                        Get<Screens.BenchmarkScreen>().StartBenchmark();
                        break;
                    }
                    case BallsNative.RequestSkipBenchmark:
                    {
                        Get<Screens.BenchmarkScreen>().CancelBenchmark();
                        break;
                    }
                    case BallsNative.RequestPickLevelFiles:
                    {
                        List<string> picked = BallsPlatform.PickLevelFiles(request.Folder);
                        if (picked != null && picked.Count > 0)
                        {
                            BallsNative.FlowImportFiles(flow, picked);
                        }
                        break;
                    }
                    case BallsNative.RequestOpenFolder:
                    {
                        BallsPlatform.OpenFolder(request.Folder);
                        break;
                    }
                    case BallsNative.RequestQuit:
                    {
                        Application.Quit();
                        break;
                    }
                }
            }
        }

        private void SyncScreenToFlow()
        {
            Type targetType = flowState.screen switch
            {
                BallsNative.ScreenMainMenu => typeof(Screens.MainMenuScreen),
                BallsNative.ScreenLevelSelect => typeof(Screens.LevelSelectScreen),
                BallsNative.ScreenGameplay => typeof(Screens.GameplayScreen),
                BallsNative.ScreenLoadError => typeof(Screens.LoadErrorScreen),
                BallsNative.ScreenMapEditor => typeof(Screens.EditorScreen),
                BallsNative.ScreenSettings => typeof(Screens.SettingsScreen),
                BallsNative.ScreenMachineBenchmark => typeof(Screens.BenchmarkScreen),
                _ => typeof(Screens.MainMenuScreen),
            };

            if (current == null || current.GetType() != targetType)
            {
                if (screens.TryGetValue(targetType, out Screen screen))
                {
                    current?.Leave();
                    current = screen;
                    current.Enter();
                }
            }
        }

        public void OnGUI()
        {
            Theme.Resize(UnityEngine.Screen.width, UnityEngine.Screen.height);
            if (current == null)
            {
                return;
            }
            current.Draw();
            current.DrawOverlay();
            if (flowState.toastSecondsLeft > 0f)
            {
                string toastText = BallsNative.FlowText(flow, BallsNative.FlowTextToast);
                if (!string.IsNullOrEmpty(toastText))
                {
                    Widgets.Toast(new Rect(0f, 24f, Theme.UnitsWidth, 44f), toastText, flowState.toastIsError != 0);
                }
            }
        }

        public string LevelDirectoryFor(int mode)
        {
            string root = Application.isEditor
                ? Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "..", "data", "assets", "levels"))
                : Path.Combine(Application.streamingAssetsPath, "levels");
            return Path.Combine(root, mode == PerformanceMode ? "performance" : "normal");
        }

        public void SetMode(int mode)
        {
            Mode = mode;
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_show_level_select(flow, mode);
            }
        }

        public void StartGameplay(int mode, string levelName, int backendKind = -1)
        {
            if (backendKind >= 0)
            {
                BackendKind = backendKind;
            }
            Mode = mode;
            LevelName = levelName;
            LevelDirectory = LevelDirectoryFor(mode);
            if (flow != IntPtr.Zero)
            {
                BallsNative.balls_flow_start_game(flow, mode, levelName);
            }
        }

        public void ApplyBallCollisions(bool enabled)
        {
            if (Running)
            {
                BallsInput.RequestCollisions(enabled);
            }
        }

        public void SetBackend(int kind) => BackendKind = kind;

        public void SwitchBackend(int kind)
        {
            BackendKind = kind;
            BallsInput.RequestBackendSwitch(kind);
        }

        public bool LoadSettings()
        {
            var settings = new NativeSettings();
            string path = SettingsPath;
            if (BallsNative.balls_settings_load(path, out settings) != 0)
            {
                settings = new NativeSettings { defaultBackend = new byte[64], orientation = 0 };
            }
            Settings = new SettingsData(settings, path);
            Profile = MachineProfile.Load();
            BackendKind = DefaultBackendKind;
            return true;
        }

        public bool SaveSettings()
        {
            Settings?.Write();
            return true;
        }

        public void ApplyOrientation()
        {
            if (Settings == null)
            {
                return;
            }
            if (Application.isMobilePlatform)
            {
                UnityEngine.Screen.orientation = Settings.Orientation switch
                {
                    SettingsData.OrientationLandscape => UnityEngine.ScreenOrientation.LandscapeLeft,
                    SettingsData.OrientationPortrait => UnityEngine.ScreenOrientation.Portrait,
                    _ => UnityEngine.ScreenOrientation.AutoRotation,
                };
            }
#if UNITY_STANDALONE
            else if (UnityEngine.Screen.fullScreenMode == FullScreenMode.Windowed &&
                     (Settings.Orientation == SettingsData.OrientationLandscape ||
                      Settings.Orientation == SettingsData.OrientationPortrait))
            {
                var resolution = UnityEngine.Screen.currentResolution;
                int width = Settings.Orientation == SettingsData.OrientationPortrait
                    ? Mathf.RoundToInt(resolution.height * 9f / 16f)
                    : 1440;
                int height = Settings.Orientation == SettingsData.OrientationPortrait
                    ? resolution.height * 9 / 10
                    : 900;
                UnityEngine.Screen.SetResolution(width, height, false);
            }
#else
            else if (Settings.Orientation == SettingsData.OrientationLandscape ||
                     Settings.Orientation == SettingsData.OrientationPortrait)
            {
                Toast("Forced window orientation is unsupported on this platform", true);
            }
#endif
        }

        public string SettingsPath => Path.Combine(Application.persistentDataPath, "balls-settings.txt");

        public bool TryReadSession(out BoardInfoData board, out SessionInfo session, out SessionStatsData stats)
        {
            board = default;
            session = default;
            stats = default;
            World world = World.DefaultGameObjectInjectionWorld;
            if (world == null)
            {
                return false;
            }
            var em = world.EntityManager;
            using var query = em.CreateEntityQuery(typeof(BoardInfoData));
            if (query.IsEmptyIgnoreFilter)
            {
                return false;
            }
            Entity entity = query.GetSingletonEntity();
            board = em.GetComponentData<BoardInfoData>(entity);
            session = em.GetComponentData<SessionInfo>(entity);
            stats = em.GetComponentData<SessionStatsData>(entity);
            return true;
        }

        public bool TryReadCells(List<CellElement> destination)
        {
            destination.Clear();
            World world = World.DefaultGameObjectInjectionWorld;
            if (world == null)
            {
                return false;
            }
            var em = world.EntityManager;
            using var query = em.CreateEntityQuery(typeof(BoardInfoData));
            if (query.IsEmptyIgnoreFilter)
            {
                return false;
            }
            var buffer = em.GetBuffer<CellElement>(query.GetSingletonEntity(), true);
            for (int index = 0; index < buffer.Length; ++index)
            {
                destination.Add(buffer[index]);
            }
            return true;
        }

        public bool TryReadLaunchers(out Unity.Collections.NativeArray<LauncherElement> copy, out Unity.Collections.Allocator allocator)
        {
            copy = default;
            allocator = Unity.Collections.Allocator.Temp;
            World world = World.DefaultGameObjectInjectionWorld;
            if (world == null)
            {
                return false;
            }
            var em = world.EntityManager;
            using var query = em.CreateEntityQuery(typeof(BoardInfoData));
            if (query.IsEmptyIgnoreFilter)
            {
                return false;
            }
            var buffer = em.GetBuffer<LauncherElement>(query.GetSingletonEntity(), true);
            copy = new Unity.Collections.NativeArray<LauncherElement>(buffer.Length, allocator);
            for (int index = 0; index < buffer.Length; ++index)
            {
                copy[index] = buffer[index];
            }
            return true;
        }

        public void ApplyLaunchOptions(string[] args)
        {
            string requestedBackend = null;
            foreach (string arg in args)
            {
                if (arg.StartsWith("-BallsBackend=", StringComparison.OrdinalIgnoreCase))
                {
                    requestedBackend = arg.Substring("-BallsBackend=".Length);
                }
            }
            if (!string.IsNullOrEmpty(requestedBackend))
            {
                for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
                {
                    if (BallsNative.BackendName(kind).Equals(requestedBackend, StringComparison.OrdinalIgnoreCase))
                    {
                        BackendKind = kind;
                        break;
                    }
                }
            }

            for (int index = 0; index < args.Length; ++index)
            {
                string arg = args[index];
                if (arg.StartsWith("-BallsPlay=", StringComparison.OrdinalIgnoreCase))
                {
                    string[] parts = arg.Substring("-BallsPlay=".Length).Split(':');
                    if (parts.Length >= 2)
                    {
                        int mode = parts[0].Equals("performance", StringComparison.OrdinalIgnoreCase) ? PerformanceMode : NormalMode;
                        if (flow != IntPtr.Zero)
                        {
                            BallsNative.balls_flow_start_game(flow, mode, parts[1]);
                        }
                    }
                }
                else if (arg.Equals("-BallsBenchmark", StringComparison.OrdinalIgnoreCase))
                {
                    if (flow != IntPtr.Zero)
                    {
                        BallsNative.balls_flow_run_machine_benchmark(flow);
                    }
                }
                else if (arg.Equals("-BallsEditor", StringComparison.OrdinalIgnoreCase))
                {
                    if (flow != IntPtr.Zero)
                    {
                        BallsNative.balls_flow_open_editor(flow, NormalMode, null);
                    }
                }
                else if (arg.Equals("-BallsSettings", StringComparison.OrdinalIgnoreCase))
                {
                    if (flow != IntPtr.Zero)
                    {
                        BallsNative.balls_flow_show_settings(flow);
                    }
                }
            }
        }
    }
}
