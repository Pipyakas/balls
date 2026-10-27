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

        public string StartupError { get; set; } = "";

        public bool Running => current is Screens.GameplayScreen;

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
            if (pending != null)
            {
                current?.Leave();
                current = pending;
                pending = null;
                current.Enter();
            }
            // Input the screens do not own: the capture agent's launcher clicks and the front end's global keys.
            if (current is Screens.GameplayScreen gameplay)
            {
                gameplay.HandleWorldInput();
            }
            else if (Input.GetKeyDown(KeyCode.Escape) && current != null)
            {
                Go<Screens.MainMenuScreen>();
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
            });
            Go<Screens.GameplayScreen>();
            var gameplay = Get<Screens.GameplayScreen>();
            gameplay.BeginSession();
            Toast($"{BallsNative.BackendName(backendKind)} physics");
        }

        public void RestartGameplay()
        {
            BallsInput.RequestRestart();
            Get<Screens.GameplayScreen>().OnRestarted();
        }

        public void SwitchBackend(int kind)
        {
            BackendKind = kind;
            BallsInput.RequestBackendSwitch(kind);
            Toast($"Physics: {BallsNative.BackendName(kind)}");
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
            BackendKind = Settings.BackendKind(BallsNative.BackendAvx2);
            return true;
        }

        public bool SaveSettings()
        {
            Settings?.Write();
            return true;
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
                else if (arg.Equals("-BallsEditor", StringComparison.OrdinalIgnoreCase))
                {
                    Go<Screens.EditorScreen>();
                }
                else if (arg.StartsWith("-BallsBackend=", StringComparison.OrdinalIgnoreCase))
                {
                    string name = arg.Substring("-BallsBackend=".Length);
                    for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
                    {
                        if (BallsNative.BackendName(kind).Equals(name, StringComparison.OrdinalIgnoreCase))
                        {
                            BackendKind = kind;
                        }
                    }
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
