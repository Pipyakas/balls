using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using Balls.Ui;
using Unity.Collections;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's gameplay screen: header, board, interaction gadget and debug gadget laid out by the core, plus the
    /// pause and win overlays. The board itself is drawn by the render systems; this only positions the camera over
    /// the layout's board frame and draws the UI around it.
    /// </summary>
    public sealed class GameplayScreen : Screen
    {
        // The game's interaction-gadget metrics.
        private const float LauncherMaxSize = 110f;
        private const float LauncherGapShare = 0.18f;
        private const float GadgetPadding = 12f;
        private const int LauncherBallsPerLauncher = 1;

        private CoreLayout layout;
        private bool hasLayout;
        private bool loggedCamera;
        private int layoutColumns;
        private int layoutRows;
        private int layoutLaunchers;

        private bool paused;
        private bool settingsOpen;
        private bool wonShown;
        private int probeTargetFps = 60;
        private int spawnRateIndex;
        private int multiplierIndex;
        private int limitIndex;
        private Vector2 interactionScroll;
        private Vector2 debugScroll;
        private readonly string[] spawnRateOptions = { "10", "50", "200", "1000", "2000", "Max" };
        private readonly string[] multiplierOptions = { "x1", "x10", "x100", "x1000", "x10000" };
        private readonly string[] limitOptions = { "Auto", "10k", "100k", "1M", "10M", "Max" };

        public void BeginSession()
        {
            paused = false;
            settingsOpen = false;
            wonShown = false;
            hasLayout = false;
            interactionScroll = Vector2.zero;
            debugScroll = Vector2.zero;
            probeTargetFps = 60;
            if (SessionSystem.Current?.Backend != null)
            {
                spawnRateIndex = SessionSystem.Current.Backend.SpawnRateIndex;
                multiplierIndex = SessionSystem.Current.Backend.BallMultiplierIndex;
                limitIndex = SessionSystem.Current.Backend.BallLimitIndex;
            }
        }

        public void OnRestarted()
        {
            wonShown = false;
            paused = false;
            settingsOpen = false;
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            if (!App.TryReadSession(out BoardInfoData board, out SessionInfo session, out SessionStatsData stats) || !board.valid)
            {
                theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);
                App.Widgets.Label(new Rect(0f, theme.UnitsHeight * 0.45f, theme.UnitsWidth, 40f),
                    "Loading level…", TypeSizes.Big, Palette.TextMuted, TextAnchor.MiddleCenter);
                return;
            }

            // No full-window fill here: the board is drawn by the camera underneath this UI, and the gadgets and
            // header paint their own panels.
            EnsureLayout(board);

            RefitCamera(board);

            DrawHeader(session, board);

            if (session.mode == BallsApp.PerformanceMode)
            {
                DrawPerformanceGadget(board, session);
            }
            else
            {
                DrawLauncherGadget(board, session);
            }
            DrawDebugGadget(board, session, stats);

            if (session.state == BallsNative.StateWon && App.Mode == BallsApp.NormalMode)
            {
                wonShown = true;
            }
        }

        public override void DrawOverlay()
        {
            if (settingsOpen)
            {
                App.Get<SettingsScreen>().DrawAsOverlay(() => settingsOpen = false);
                return;
            }
            if (wonShown)
            {
                DrawWinOverlay();
                return;
            }
            if (paused)
            {
                DrawPauseOverlay();
            }
        }

        /// <summary>Launcher clicks and keys that are not the screens' own.</summary>
        public void HandleWorldInput()
        {
            if (paused || settingsOpen || wonShown)
            {
                return;
            }
            if (Input.GetKeyDown(KeyCode.Escape) || Input.GetKeyDown(KeyCode.P))
            {
                paused = true;
                return;
            }
            if (Input.GetKeyDown(KeyCode.Space) && App.Mode == BallsApp.PerformanceMode)
            {
                BallsInput.RequestAutoRelease();
            }
            if (Input.GetKeyDown(KeyCode.F1))
            {
                App.SwitchBackend(BallsNative.BackendScalar);
            }
            if (Input.GetKeyDown(KeyCode.F2))
            {
                App.SwitchBackend(BallsNative.BackendAvx2);
            }
        }

        private void EnsureLayout(BoardInfoData board)
        {
            int launcherRows = Mathf.Max(1, board.launcherVisibleRows);
            if (hasLayout && layoutColumns == board.columns && layoutRows == board.rows &&
                layoutLaunchers == launcherRows)
            {
                return;
            }
            layout = CoreLayout.Compute(App.Theme.UnitsWidth, App.Theme.UnitsHeight, board.columns, board.rows,
                App.Settings?.Orientation ?? SettingsData.OrientationAuto, launcherRows);
            layoutColumns = board.columns;
            layoutRows = board.rows;
            layoutLaunchers = launcherRows;
            hasLayout = true;
        }

        /// <summary>
        /// Pins the board's world rectangle to the layout's board frame: the camera's viewport becomes that pixel
        /// rectangle and its height that many cells, so the two can never drift apart.
        /// </summary>
        /// <summary>
        /// Maps world cells onto the layout's board frame. The camera keeps the whole window and its projection is
        /// derived from the layout, which avoids the trap of Camera.rect: the viewport shrinks but Unity keeps the
        /// screen aspect, so a tall board frame would stretch everything drawn inside it.
        /// </summary>
        private void RefitCamera(BoardInfoData board)
        {
            Camera camera = App.Camera;
            if (camera == null || layout.CellSize <= 0f)
            {
                return;
            }
            UiTheme theme = App.Theme;
            float cell = layout.CellSize;
            Rect boardRect = layout.BoardRect;
            // One world cell is `cell` layout units, so the window is this many cells tall.
            float windowCells = theme.UnitsHeight / cell;
            camera.orthographic = true;
            camera.orthographicSize = windowCells * 0.5f;
            camera.rect = new Rect(0f, 0f, 1f, 1f);
            camera.transform.position = new Vector3(
                theme.UnitsWidth / (2f * cell) - boardRect.x / cell,
                (boardRect.y + boardRect.height) / cell - windowCells * 0.5f,
                -10f);
            camera.transform.rotation = Quaternion.identity;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Palette.BoardBackdrop;
            camera.nearClipPlane = 0.01f;
            camera.farClipPlane = 100f;
            _ = board;
        }

        private void DrawHeader(SessionInfo session, BoardInfoData board)
        {
            Widgets widgets = App.Widgets;
            Rect header = layout.Header;
            string modeName = session.mode == BallsApp.PerformanceMode ? "Performance" : "Normal";
            GUI.Label(App.Theme.Units(header.x, header.y, header.width - 120f, header.height),
                $"{modeName} · {App.LevelName}",
                App.Theme.TextStyle(TypeSizes.Header, Palette.Text, false, TextAnchor.MiddleLeft, true));

            Rect pauseButton = new Rect(header.xMax - 104f, header.y + (header.height - 36f) * 0.5f, 104f, 36f);
            if (widgets.AccentButton(pauseButton, paused ? "Resume" : "Pause", paused, TypeSizes.Value))
            {
                paused = !paused;
            }
        }

        private void DrawLauncherGadget(BoardInfoData board, SessionInfo session)
        {
            Widgets widgets = App.Widgets;
            Rect gadget = layout.Interaction;
            Rect inner = widgets.Gadget(gadget, null);
            widgets.Label(new Rect(inner.x, inner.y, inner.width, 24f), "Launchers", TypeSizes.Value, Palette.Accent, TextAnchor.UpperLeft, true);
            Rect grid = new Rect(inner.x, inner.y + 30f, inner.width, inner.height - 30f);

            if (session.launcherRemaining <= 0)
            {
                widgets.Muted(new Rect(grid.x, grid.y + 8f, grid.width, 28f), "All launchers released", TypeSizes.Medium);
                return;
            }

            if (!App.TryReadLaunchers(out NativeArray<LauncherElement> launchers, out Allocator allocator))
            {
                return;
            }
            try
            {
                int columns = Mathf.Max(1, board.launcherColumns);
                int rows = Mathf.Max(1, board.launcherVisibleRows);
                float gaps = columns - 1;
                float size = Mathf.Min(LauncherMaxSize, grid.width / (columns + gaps * LauncherGapShare));
                float gap = size * LauncherGapShare;
                float totalWidth = columns * size + gaps * gap;
                float startX = grid.x + Mathf.Max(0f, (grid.width - totalWidth) * 0.5f);
                float startY = grid.y + 8f;

                for (int row = 0; row < rows; ++row)
                {
                    for (int column = 0; column < columns; ++column)
                    {
                        int index = row * columns + column;
                        if (index >= launchers.Length || !launchers[index].present)
                        {
                            continue;
                        }
                        LauncherElement launcher = launchers[index];
                        Rect slot = new Rect(startX + column * (size + gap), startY + row * (size + gap), size, size);
                        if (slot.yMax > grid.yMax)
                        {
                            continue;
                        }
                        DrawLauncherSlot(widgets, launcher, slot, row == 0);
                    }
                }
            }
            finally
            {
                launchers.Dispose();
            }
        }

        private void DrawLauncherSlot(Widgets widgets, LauncherElement launcher, Rect slot, bool front)
        {
            Color color = Palette.Balls[Mathf.Clamp(launcher.color, 0, Palette.Balls.Length - 1)];
            float fade = front ? 1f : 0.55f;
            App.Theme.Frame(slot, UiTheme.WithAlpha(color, fade), UiTheme.WithAlpha(Color.white, 0.10f));
            widgets.Label(new Rect(slot.x, slot.y + slot.height * 0.5f - 12f, slot.width, 24f),
                launcher.ballCount.ToString(), TypeSizes.Label, Palette.Background, TextAnchor.MiddleCenter, true);

            if (GUI.Button(App.Theme.Units(slot.x, slot.y, slot.width, slot.height), GUIContent.none, GUIStyle.none))
            {
                BallsInput.ClickLauncher(launcher.column, launcher.row);
            }
        }

        private void DrawPerformanceGadget(BoardInfoData board, SessionInfo session)
        {
            Widgets widgets = App.Widgets;
            GameBackend backend = SessionSystem.Current?.Backend;
            Rect gadget = layout.Interaction;
            Rect inner = widgets.Gadget(gadget, null);

            float contentHeight = 470f;
            float y = 0f;
            widgets.ScrollColumn(inner, contentHeight, ref interactionScroll, content =>
            {
                y = content.y;
                if (session.launcherRemaining > 0 && !IsAutoReleasing(session))
                {
                    if (widgets.AccentButton(new Rect(content.x, y, content.width, 52f), "Start", true, TypeSizes.Medium))
                    {
                        BallsInput.RequestAutoRelease();
                    }
                }
                else
                {
                    widgets.Label(new Rect(content.x, y, content.width, 52f),
                        $"Auto releasing — {session.launcherRemaining} launchers left", TypeSizes.Label,
                        Palette.TextMuted, TextAnchor.MiddleLeft);
                }
                y += 62f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Capacity probe", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                if (widgets.Stepper(new Rect(content.x, y, content.width, 36f), "Target FPS", ref probeTargetFps, 10, 1000, out _))
                {
                    if (backend != null)
                    {
                        backend.StartProbe(probeTargetFps);
                    }
                }
                y += 42f;
                string probeText = backend == null ? "" :
                    backend.ProbeActive ? $"Probing… {backend.ProbeBest:N0} balls, {(int)backend.ProbeFps} fps"
                    : backend.ProbeBest > 0 ? $"Max @{(int)backend.ProbeTargetFps} FPS: {backend.ProbeBest:N0}"
                    : $"Target {(int)backend.ProbeTargetFps} FPS";
                if (widgets.Button(new Rect(content.x, y, content.width, 40f), "Find max balls", TypeSizes.Label))
                {
                    backend?.StartProbe(probeTargetFps);
                }
                widgets.Muted(new Rect(content.x, y + 44f, content.width, 22f), probeText, TypeSizes.Small);
                y += 74f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Spawn", TypeSizes.Small, Palette.Accent, TextAnchor.UpperLeft, true);
                y += 24f;
                if (widgets.Dropdown("spawnrate", new Rect(content.x, y, content.width, 34f), spawnRateOptions, ref spawnRateIndex))
                {
                    backend?.SelectSpawnRate(spawnRateIndex);
                }
                y += 42f;
                if (widgets.Dropdown("multiplier", new Rect(content.x, y, content.width, 34f), multiplierOptions, ref multiplierIndex))
                {
                    backend?.SelectBallMultiplier(multiplierIndex);
                }
                y += 42f;
                if (widgets.Dropdown("balllimit", new Rect(content.x, y, content.width, 34f), limitOptions, ref limitIndex))
                {
                    backend?.SelectBallLimit(limitIndex);
                }
                y += 50f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Physics", TypeSizes.Small, Palette.Accent, TextAnchor.UpperLeft, true);
                y += 24f;
                for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
                {
                    if (kind == BallsNative.BackendHybrid && BallsNative.balls_backend_available(kind) == 0)
                    {
                        continue;
                    }
                    bool available = BallsNative.balls_backend_available(kind) != 0;
                    string detail = available ? BallsNative.BackendText(kind, true) : BallsNative.BackendText(kind, false);
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), BallsNative.BackendName(kind),
                            session.backend == kind, available, detail) && available)
                    {
                        App.SwitchBackend(kind);
                    }
                    y += 38f;
                }
            });
        }

        private static bool IsAutoReleasing(SessionInfo session) => session.state == BallsNative.StateRunning;

        private void DrawDebugGadget(BoardInfoData board, SessionInfo session, SessionStatsData stats)
        {
            Widgets widgets = App.Widgets;
            Rect gadget = layout.Debug;
            Rect inner = widgets.Gadget(gadget, null);
            float contentHeight = 620f;
            widgets.ScrollColumn(inner, contentHeight, ref debugScroll, content =>
            {
                float y = content.y;
                float half = (content.width - 12f) * 0.5f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "FPS", $"{stats.fps:F0}",
                    stats.fps >= 60f ? Palette.Success : Palette.Danger);
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Min FPS (1s)", $"{stats.frameMinFps:F0}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Frame ms", $"{stats.frameMs:F2}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Physics ms", $"{stats.physicsMs:F2}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Backend", BallsNative.BackendName(session.backend));
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Ball collisions",
                    stats.collisions ? "On" : "Off");
                y += 52f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Objects", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 24f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Balls", $"{stats.liveBalls:N0}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Bricks", $"{board.aliveBricks:N0}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Launchers", $"{session.launcherRemaining:N0}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Waiting to spawn",
                    $"{stats.waitingToSpawn:N0}");
                y += 52f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Balls per color", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 24f;
                for (int color = 0; color < Palette.Balls.Length; ++color)
                {
                    App.Theme.FillDisc(new Rect(content.x, y + 6f, 14f, 14f), Palette.Balls[color]);
                    widgets.Label(new Rect(content.x + 22f, y, content.width - 22f, 22f), PaletteColorName(color),
                        TypeSizes.Small, Palette.TextMuted);
                    widgets.Label(new Rect(content.x, y, content.width, 22f), $"{stats.ByColor(color):N0}",
                        TypeSizes.Small, Palette.Text, TextAnchor.UpperRight);
                    y += 24f;
                }
            });
        }

        private static string PaletteColorName(int color) => color switch
        {
            0 => "Red",
            1 => "Orange",
            2 => "Yellow",
            3 => "Green",
            4 => "Blue",
            5 => "Indigo",
            6 => "Violet",
            _ => "?",
        };

        private void DrawPauseOverlay()
        {
            Widgets widgets = App.Widgets;
            UiTheme theme = App.Theme;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), new Color(0f, 0f, 0f, 0.70f));
            float width = 420f;
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = theme.UnitsHeight * 0.32f;
            Rect panel = new Rect(x, y, width, 60f + 4 * (60f + 14f) + 24f);
            theme.Frame(panel, Palette.Panel, Palette.Border);
            widgets.Label(new Rect(panel.x, panel.y + 16f, panel.width, 44f), "Paused", TypeSizes.Large,
                Palette.Text, TextAnchor.MiddleCenter, true);

            float buttonY = panel.y + 70f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Resume"))
            {
                paused = false;
            }
            buttonY += 74f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Restart"))
            {
                paused = false;
                App.RestartGameplay();
            }
            buttonY += 74f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Settings"))
            {
                settingsOpen = true;
            }
            buttonY += 74f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Main Menu"))
            {
                paused = false;
                App.Go<MainMenuScreen>();
            }
        }

        private void DrawWinOverlay()
        {
            Widgets widgets = App.Widgets;
            UiTheme theme = App.Theme;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), new Color(0f, 0f, 0f, 0.70f));
            float width = 420f;
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = theme.UnitsHeight * 0.32f;
            Rect panel = new Rect(x, y, width, 60f + 3 * (60f + 14f) + 24f);
            theme.Frame(panel, Palette.Panel, Palette.Border);
            widgets.Label(new Rect(panel.x, panel.y + 16f, panel.width, 44f), "Level Complete!", TypeSizes.Large,
                Palette.Accent, TextAnchor.MiddleCenter, true);

            float buttonY = panel.y + 70f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Next Level"))
            {
                wonShown = false;
                App.Go<LevelSelectScreen>();
            }
            buttonY += 74f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Restart"))
            {
                wonShown = false;
                App.RestartGameplay();
            }
            buttonY += 74f;
            if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), "Main Menu"))
            {
                wonShown = false;
                App.Go<MainMenuScreen>();
            }
        }
    }
}
