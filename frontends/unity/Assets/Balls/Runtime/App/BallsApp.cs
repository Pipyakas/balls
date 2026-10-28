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
    /// <summary>
    /// The screen stack and the state every screen shares: the mode, the level being played, the settings, the toast
    /// and the camera. One of these exists per run; <see cref="BallsBootstrap"/> forwards Unity's Update and OnGUI.
    /// </summary>
    public sealed class BallsApp
    {
        public const int NormalMode = BallsNative.ModeNormal;
        public const int PerformanceMode = BallsNative.ModePerformance;

        private struct ToastMessage
        {
            public string text;
            public bool error;
            public float until;
        }

        private readonly Dictionary<Type, Screen> screens = new();
        private Screen current;
        private Screen pending;
        private ToastMessage toast;
        private readonly List<LevelInfo> levels = new();
        private readonly string[] levelNames = Array.Empty<string>();

        public BallsApp()
        {
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

        /// <summary>
        /// Which backend a new game starts on: the Settings default, else the target PC winner, else the fastest CPU
        /// backend that loads (AVX2, then Scalar). Switching inside a game never changes it.
        /// </summary>
        public int DefaultBackendKind => Settings != null ? Settings.BackendKind(FallbackBackend()) : FallbackBackend();

        private static int FallbackBackend()
        {
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

        /// <summary>Set while a headless capture is pending, so screens stay put instead of auto-advancing.</summary>
        public static bool HoldingForCapture { get; set; }

        public bool Running => current is Screens.GameplayScreen;

        /// <summary>The HTTP control API, or null when this run did not ask for one.</summary>
        public ControlApi Control { get; private set; }

        /// <summary>The screen on show, as the control API names it.</summary>
        public string ScreenName => current switch
        {
            Screens.GameplayScreen => "gameplay",
            Screens.LevelSelectScreen => "level-select",
            Screens.SettingsScreen => "settings",
            Screens.EditorScreen => "editor",
            Screens.BenchmarkScreen => "benchmark",
            Screens.LoadErrorScreen => "load-error",
            _ => "main-menu",
        };

        /// <summary>The toast still on screen, or empty; benchmarks read it to tell what a step said.</summary>
        public string ToastText => toast.until > Time.realtimeSinceStartup ? toast.text : string.Empty;

        /// <summary>
        /// The game's Renderer value, renderer / API / adapter in one string ("Unity URP / Vulkan / AMD Radeon ..."); the
        /// debug gadget and /state show the same text so benchmark runs record what they measured.
        /// </summary>
        public static string RendererText =>
            rendererText ??= $"Unity URP / {SystemInfo.graphicsDeviceType} / {SystemInfo.graphicsDeviceName}";

        private static string rendererText;

        /// <summary>Starts the control API on a loopback port; 0 leaves it off.</summary>
        public void StartControlApi(int port)
        {
            if (port <= 0 || Control != null)
            {
                return;
            }
            Control = new ControlApi(this, port);
            Control.Start();
        }

        /// <summary>Smoothed GPU frame time from Unity's frame timing, or 0 when the platform reports none.</summary>
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

        public void Go<T>() where T : Screen
        {
            if (!screens.TryGetValue(typeof(T), out Screen screen))
            {
                Debug.LogError($"no screen registered for {typeof(T).Name}");
                return;
            }
            pending = screen;
        }

        public void Go(Screen screen) => pending = screen;

        public void Toast(string text, bool error = false)
        {
            toast = new ToastMessage { text = text, error = error, until = Time.realtimeSinceStartup + 3f };
        }

        public void Update()
        {
            SampleGpuTime();
            Control?.Pump();
            if (pending != null)
            {
                current?.Leave();
                current = pending;
                pending = null;
                current.Enter();
            }
            if (current != null && Input.GetKeyDown(KeyCode.Escape))
            {
                current.HandleEscape();
            }
            string loadFailure = SessionSystem.Current?.LoadFailure;
            if (current is Screens.GameplayScreen && loadFailure != null)
            {
                Get<Screens.LoadErrorScreen>().Show(loadFailure);
                Go<Screens.LoadErrorScreen>();
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
            if (toast.until > Time.realtimeSinceStartup)
            {
                Widgets.Toast(new Rect(0f, 24f, Theme.UnitsWidth, 44f), toast.text, toast.error);
            }
        }

        /// <summary>Where this mode's level files live: repo data in the editor, StreamingAssets in a build.</summary>
        public string LevelDirectoryFor(int mode)
        {
            string root = Application.isEditor
                ? Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "data", "assets", "levels"))
                : Path.Combine(Application.streamingAssetsPath, "levels");
            return Path.Combine(root, mode == PerformanceMode ? "performance" : "normal");
        }

        public IReadOnlyList<string> LevelNames()
        {
            levels.Clear();
            string directory = LevelDirectoryFor(Mode);
            int count = BallsNative.balls_levels_find(directory, null, 0);
            if (count > 0)
            {
                var buffer = new NativeLevelInfo[count];
                BallsNative.balls_levels_find(directory, buffer, count);
                foreach (var info in buffer)
                {
                    levels.Add(new LevelInfo(info.Name, info.BricksPath, info.LaunchersPath));
                }
            }
            levelNamesCache = new string[levels.Count];
            for (int index = 0; index < levels.Count; ++index)
            {
                levelNamesCache[index] = levels[index].Name;
            }
            return levelNamesCache;
        }

        private string[] levelNamesCache = Array.Empty<string>();

        public void SetMode(int mode) => Mode = mode;

        public void StartGameplay(int mode, string levelName, int backendKind)
        {
            bool collisions = Settings != null && Settings.BallCollisions;
            if (collisions && backendKind != BallsNative.BackendScalar)
            {
                backendKind = BallsNative.BackendScalar;
                Toast(CollisionsNeedScalar);
            }
            if (SessionSystem.Current != null)
            {
                SessionSystem.Current.LoadFailure = null;
            }
            Mode = mode;
            LevelName = levelName;
            BackendKind = backendKind;
            LevelDirectory = LevelDirectoryFor(mode);
            BallsInput.RequestStart(new BallsInput.StartRequest
            {
                mode = mode,
                levelDirectory = LevelDirectory,
                levelName = levelName,
                backend = backendKind,
                collisions = collisions,
            });
            Go<Screens.GameplayScreen>();
            var gameplay = Get<Screens.GameplayScreen>();
            gameplay.BeginSession();
            if (!collisions)
            {
                Toast($"{BallsNative.BackendName(backendKind)} physics");
            }
        }

        public const string CollisionsNeedScalar = "Ball collisions need the Scalar backend - switched to Scalar";

        /// <summary>Applies the Ball collisions setting to the running game; the Scalar backend is the only one that supports it.</summary>
        public void ApplyBallCollisions(bool enabled)
        {
            if (!Running)
            {
                return;
            }
            if (enabled && BackendKind != BallsNative.BackendScalar)
            {
                BackendKind = BallsNative.BackendScalar;
                BallsInput.RequestBackendSwitch(BallsNative.BackendScalar);
                Toast(CollisionsNeedScalar);
            }
            BallsInput.RequestCollisions(enabled);
        }

        /// <summary>The level after the current one in the mode's list, or Level select after the last.</summary>
        public void StartNextLevel()
        {
            IReadOnlyList<string> names = LevelNames();
            int index = -1;
            for (int i = 0; i < names.Count; ++i)
            {
                if (names[i] == LevelName)
                {
                    index = i;
                    break;
                }
            }
            if (index >= 0 && index + 1 < names.Count)
            {
                StartGameplay(Mode, names[index + 1], BackendKind);
            }
            else
            {
                Go<Screens.LevelSelectScreen>();
            }
        }

        /// <summary>Restart loads the files as they are now; with the files gone it returns to Level select.</summary>
        public void RestartGameplay()
        {
            switch (Get<Screens.GameplayScreen>().WatchState)
            {
                case LevelWatcher.State.Removed:
                    Go<Screens.LevelSelectScreen>();
                    return;
                case LevelWatcher.State.Changed:
                    StartGameplay(Mode, LevelName, BackendKind);
                    return;
            }
            BallsInput.RequestRestart();
            Get<Screens.GameplayScreen>().OnRestarted();
        }

        /// <summary>Sets the backend the next game starts on, without touching a running one.</summary>
        public void SetBackend(int kind) => BackendKind = kind;

        /// <summary>
        /// Switches a running game without blocking it: the row reads starting... until the session takes the new
        /// backend over, and GameplayScreen reports a failure; SDL behaves the same.
        /// </summary>
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

        /// <summary>Reads the session's singletons, or reports that there is none.</summary>
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

        /// <summary>Copies the board cells into <paramref name="destination"/>; false while no session is published.</summary>
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

        /// <summary>Parses the launch options the design requires every port to accept.</summary>
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
                        StartGameplay(mode, parts[1], BackendKind);
                    }
                }
                else if (arg.Equals("-BallsBenchmark", StringComparison.OrdinalIgnoreCase))
                {
                    Go<Screens.BenchmarkScreen>();
                }
                else if (arg.Equals("-BallsEditor", StringComparison.OrdinalIgnoreCase))
                {
                    Go<Screens.EditorScreen>();
                }
                else if (arg.Equals("-BallsSettings", StringComparison.OrdinalIgnoreCase))
                {
                    Go<Screens.SettingsScreen>();
                }
            }
        }
    }

    public readonly struct LevelInfo
    {
        public LevelInfo(string name, string bricksPath, string launchersPath)
        {
            Name = name;
            BricksPath = bricksPath;
            LaunchersPath = launchersPath;
        }

        public string Name { get; }

        public string BricksPath { get; }

        public string LaunchersPath { get; }
    }
}
