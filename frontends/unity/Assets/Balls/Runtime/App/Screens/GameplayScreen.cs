using System.Collections.Generic;
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
        private const float PortraitDebugHeight = 48f;
        private const float LauncherBackendHeight = 24f + BallsNative.BackendCount * 38f + 12f;
        private const float LauncherHeaderHeight = 24f;
        private const float BrickLabelShare = 0.55f;
        private const float MinimumLabelPixels = 9f;
        private const float ShadowMinimumCellPixels = 28f;
        private static readonly string[] HitLabels = new string[128];

        /// <summary>Hit counts as strings, built once, so the per-frame labels allocate nothing.</summary>
        private static string HitLabel(int hits)
        {
            if (hits < 0 || hits >= HitLabels.Length)
            {
                return hits.ToString();
            }
            return HitLabels[hits] ??= hits.ToString();
        }
        private const float TwoColumnWidth = 2f * 220f + 16f;
        private const float ProbeContentHeight = 204f;
        private const float SpawnContentHeight = 158f;
        private const float GpuChoicesHeight = 84f;
        private const int LauncherBallsPerLauncher = 1;

        private CoreLayout layout;
        private bool hasLayout;
        private bool loggedCamera;
        private int layoutColumns;
        private int layoutRows;
        private int layoutLaunchers;
        private int layoutLauncherColumns;
        private int layoutOrientation = -1;
        private float lastLayoutWidth = -1f;
        private float lastLayoutHeight = -1f;

        private readonly LevelWatcher watcher = new();
        private readonly List<CellElement> labelCells = new();
        private bool paused;
        private bool settingsOpen;
        private bool wonShown;
        public bool Paused => paused || settingsOpen || wonShown;
        private int probeTargetFps = 60;
        private int spawnRateIndex;
        private int multiplierIndex;
        private int limitIndex;
        private bool autoLimit;
        private bool probeRecorded;
        private GameBackend syncedBackend;
        private Vector2 interactionScroll;
        private Vector2 debugScroll;
        private readonly string[] spawnRateOptions = { "10", "50", "200", "1000", "2000", "Max" };
        private readonly string[] multiplierOptions = { "x1", "x10", "x100", "x1000", "x10000" };
        private readonly string[] limitOptions = { "Auto", "10k", "100k", "1M", "10M", "Max" };

        public void BeginSession()
        {
            watcher.Watch(App.LevelDirectory, App.LevelName);
            paused = false;
            settingsOpen = false;
            wonShown = false;
            hasLayout = false;
            layoutOrientation = -1;
            lastLayoutWidth = -1f;
            lastLayoutHeight = -1f;
            interactionScroll = Vector2.zero;
            debugScroll = Vector2.zero;
            probeTargetFps = 60;
            syncedBackend = null;
        }

        /// <summary>
        /// Mirrors SDL: a backend switch finishes off-frame, a failure is told rather than silently ignored, and the
        /// new backend's saved machine limit becomes the Auto limit.
        /// </summary>
        private void HandleSwitchOutcome()
        {
            GameBackend backend = SessionSystem.Current?.Backend;
            if (backend == null)
            {
                return;
            }
            int outcome = backend.TakeSwitchOutcome(out int kind);
            if (outcome == 1)
            {
                ReapplySavedLimit(backend);
            }
            else if (outcome == 2)
            {
                App.Toast($"Could not start the {BallsNative.BackendName(kind)} backend");
            }
        }

        /// <summary>Reads the performance controls back from a new session, whenever the backend object is replaced.</summary>
        private void SyncControls()
        {
            GameBackend backend = SessionSystem.Current?.Backend;
            if (backend == null || ReferenceEquals(backend, syncedBackend))
            {
                return;
            }
            syncedBackend = backend;
            spawnRateIndex = backend.SpawnRateIndex;
            multiplierIndex = backend.BallMultiplierIndex;
            limitIndex = backend.BallLimitIndex + 1;
            autoLimit = false;
            probeRecorded = false;
            ReapplySavedLimit(backend);
        }

        /// <summary>A capacity probe result saved for this backend becomes the Auto ball limit of a new Performance game.</summary>
        private void ReapplySavedLimit(GameBackend backend)
        {
            string name = BallsNative.BackendName(backend.BackendKind);
            if (backend.Mode != BallsApp.PerformanceMode || !App.Profile.TryLimit(name, out ulong balls, out double fps))
            {
                return;
            }
            backend.UseMachineLimit(balls);
            limitIndex = 0;
            autoLimit = true;
            App.Toast($"{name} limit {balls:N0} balls @{fps:F0} FPS");
        }

        /// <summary>Saves a finished capacity probe per machine and backend so the next run starts from it.</summary>
        private void RecordFinishedProbe(GameBackend backend)
        {
            const int probeDone = 4;
            if (backend.ProbePhase != probeDone)
            {
                probeRecorded = false;
                return;
            }
            if (probeRecorded || backend.ProbeBest == 0)
            {
                return;
            }
            probeRecorded = true;
            App.Profile.SetLimit(BallsNative.BackendName(backend.BackendKind), backend.ProbeBest, backend.ProbeTargetFps);
            App.Profile.Save();
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
            HandleSwitchOutcome();
            SyncControls();
            if (SessionSystem.Current?.Backend != null)
            {
                RecordFinishedProbe(SessionSystem.Current.Backend);
            }
            EnsureLayout(board);

            RefitCamera(board);
            DrawBrickLabels(board);

            DrawHeader(session, board);
            if (layout.Portrait)
            {
                DrawPortraitDebugStrip(board, session, stats);
            }

            if (session.mode == BallsApp.PerformanceMode)
            {
                DrawPerformanceGadget(board, session);
            }
            else
            {
                DrawLauncherGadget(board, session);
            }
            if (!layout.Portrait)
            {
                DrawDebugGadget(board, session, stats);
            }

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

        /// <summary>Esc pauses gameplay; an open settings overlay returns to Pause.</summary>
        public override void HandleEscape()
        {
            if (settingsOpen)
            {
                App.Get<SettingsScreen>().HandleEscape();
            }
            else if (!paused && !wonShown)
            {
                paused = true;
            }
        }

        /// <summary>The level files' state against what was loaded, for the header notice and Restart.</summary>
        public LevelWatcher.State WatchState => watcher.Poll();

        private void EnsureLayout(BoardInfoData board)
        {
            int launcherRows = Mathf.Max(1, board.launcherVisibleRows);
            int launcherColumns = Mathf.Max(1, board.launcherColumns);
            int orientation = App.Settings?.Orientation ?? SettingsData.OrientationAuto;
            float width = App.Theme.UnitsWidth;
            float height = App.Theme.UnitsHeight;
            if (hasLayout && layoutColumns == board.columns && layoutRows == board.rows &&
                layoutLaunchers == launcherRows && layoutLauncherColumns == launcherColumns && layoutOrientation == orientation &&
                Mathf.Approximately(lastLayoutWidth, width) && Mathf.Approximately(lastLayoutHeight, height))
            {
                return;
            }
            layout = CoreLayout.Compute(width, height, board.columns, board.rows, orientation, launcherRows);
            layoutColumns = board.columns;
            layoutRows = board.rows;
            layoutLaunchers = launcherRows;
            layoutLauncherColumns = launcherColumns;
            layoutOrientation = orientation;
            lastLayoutWidth = width;
            lastLayoutHeight = height;
            hasLayout = true;
            interactionScroll = Vector2.zero;
            debugScroll = Vector2.zero;
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
            if (layout.Portrait)
            {
                float frameW = lastLayoutWidth;
                float frameH = lastLayoutHeight;
                float margin = 16f;
                float headerHeight = 44f;
                float debugHeight = PortraitDebugHeight;
                float gadgetGap = 8f;
                int launcherColumns = Mathf.Max(1, board.launcherColumns);
                int launcherRows = Mathf.Max(1, board.launcherVisibleRows);
                float launcherCell = Mathf.Min(240f, (frameW - margin * 2f - (launcherColumns - 1) * 10f) / launcherColumns);
                float launcherAreaHeight = launcherRows * launcherCell + (launcherRows - 1) * 10f;
                float contentHeight = headerHeight + debugHeight + gadgetGap + board.rows * 18f + gadgetGap + launcherAreaHeight;
                float scale = Mathf.Min(1f, (frameH - margin * 2f) / contentHeight);
                float cell = Mathf.Min((frameW - 2f * margin - 20f) / board.columns,
                    (frameH - 2f * margin - headerHeight - debugHeight - 2f * gadgetGap - launcherAreaHeight) / board.rows);
                cell = Mathf.Max(0.5f, cell * scale);
                float boardW = board.columns * cell + 20f;
                float boardH = board.rows * cell + 20f;
                launcherCell = Mathf.Min(launcherCell * scale, 240f);
                launcherAreaHeight = launcherRows * launcherCell + (launcherRows - 1) * 10f;
                float groupH = headerHeight + debugHeight + gadgetGap + boardH + gadgetGap + launcherAreaHeight;
                float groupY = Mathf.Max(margin, (frameH - groupH) * 0.5f);
                float boardY = groupY + headerHeight + debugHeight + gadgetGap;
                float boardX = (frameW - boardW) * 0.5f;
                Rect boardRect = new Rect(boardX, boardY, boardW, boardH);
                Rect interaction = new Rect(margin, boardY + boardH + gadgetGap, frameW - margin * 2f, launcherAreaHeight);
                Rect debug = new Rect(margin, groupY + headerHeight, frameW - margin * 2f, debugHeight);
                layout.SetPortraitRects(new Rect(margin, groupY, frameW - margin * 2f, headerHeight),
                    new Rect(boardX, boardY, boardW, boardH), interaction, debug, boardRect,
                    new Vector2(boardX + 10f, boardY + 10f), cell);
            }
            UiTheme theme = App.Theme;
            float cellSize = layout.CellSize;
            Rect worldBoardRect = layout.BoardRect;
            // One world cell is `cell` layout units, so the window is this many cells tall.
            float windowCells = theme.UnitsHeight / cellSize;
            camera.orthographic = true;
            camera.orthographicSize = windowCells * 0.5f;
            camera.rect = new Rect(0f, 0f, 1f, 1f);
            camera.transform.position = new Vector3(
                theme.UnitsWidth / (2f * cellSize) - worldBoardRect.x / cellSize,
                (worldBoardRect.y + worldBoardRect.height) / cellSize - windowCells * 0.5f,
                -10f);
            camera.transform.rotation = Quaternion.identity;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Palette.BoardBackdrop;
            camera.nearClipPlane = 0.01f;
            camera.farClipPlane = 100f;
            _ = board;
        }

        /// <summary>Remaining hits on every brick, centred, with a drop shadow; hidden once they would be under 9 px.</summary>
        private void DrawBrickLabels(BoardInfoData board)
        {
            float cell = layout.CellSize;
            float labelSize = cell * BrickLabelShare;
            if (Event.current.type != EventType.Repaint || labelSize * App.Theme.Scale < MinimumLabelPixels ||
                !App.TryReadCells(labelCells))
            {
                return;
            }
            Rect boardRect = layout.BoardRect;
            int size = Mathf.RoundToInt(labelSize);
            bool withShadow = cell * App.Theme.Scale >= ShadowMinimumCellPixels;
            GUIStyle shadow = App.Theme.TextStyle(size, Palette.BoardBackdrop, true, TextAnchor.MiddleCenter);
            GUIStyle text = App.Theme.TextStyle(size, Color.white, true, TextAnchor.MiddleCenter);
            for (int index = 0; index < labelCells.Count; ++index)
            {
                CellElement brick = labelCells[index];
                if (brick.brick < 0 || brick.hitsLeft <= 0)
                {
                    continue;
                }
                int column = index % board.paddedWidth - 1;
                int row = index / board.paddedWidth - 1;
                float x = boardRect.x + column * cell;
                float y = boardRect.yMax - (row + 1f + brick.rise) * cell;
                string label = HitLabel(brick.hitsLeft);
                if (withShadow)
                {
                    GUI.Label(App.Theme.Units(x + 1.5f, y + 1.5f, cell, cell), label, shadow);
                }
                GUI.Label(App.Theme.Units(x, y, cell, cell), label, text);
            }
        }

        private void DrawHeader(SessionInfo session, BoardInfoData board)
        {
            Widgets widgets = App.Widgets;
            Rect header = layout.Header;
            string modeName = session.mode == BallsApp.PerformanceMode ? "Performance" : "Normal";
            float pauseWidth = 104f;
            int titleSize = layout.Portrait ? TypeSizes.Small : TypeSizes.Header;
            string notice = watcher.Poll() switch
            {
                LevelWatcher.State.Changed => "Map changed on disk - Restart to load it",
                LevelWatcher.State.Removed => "Map removed from disk - Restart unavailable",
                _ => null,
            };
            float titleHeight = notice == null ? header.height : header.height * 0.55f;
            string title = widgets.Ellipsize($"{modeName} · {App.LevelName}", titleSize, header.width - pauseWidth - 12f);
            GUI.Label(App.Theme.Units(header.x, header.y, header.width - pauseWidth - 12f, titleHeight), title,
                App.Theme.TextStyle(titleSize, Palette.Text, false, TextAnchor.MiddleLeft, true));
            if (notice != null)
            {
                GUI.Label(App.Theme.Units(header.x, header.y + titleHeight, header.width - pauseWidth - 12f,
                        header.height - titleHeight), notice,
                    App.Theme.TextStyle(TypeSizes.Tiny, Palette.Accent, false, TextAnchor.UpperLeft, true));
            }

            Rect pauseButton = new Rect(header.xMax - pauseWidth, header.y + (header.height - 36f) * 0.5f, pauseWidth, 36f);
            if (widgets.AccentButton(pauseButton, paused ? "Resume" : "Pause", paused, TypeSizes.Value))
            {
                paused = !paused;
            }
        }

        private void DrawPortraitLauncherRows(Widgets widgets, BoardInfoData board, SessionInfo session, Rect area)
        {
            if (session.launcherRemaining <= 0)
            {
                widgets.Muted(area, "All launchers released", TypeSizes.Medium, TextAnchor.MiddleCenter);
                return;
            }
            if (!App.TryReadLaunchers(out NativeArray<LauncherElement> launchers, out _))
            {
                return;
            }
            try
            {
                float gap = 10f;
                float columns = Mathf.Max(1, board.launcherColumns);
                float rows = Mathf.Max(1, board.launcherVisibleRows);
                float maxSize = Mathf.Min(240f, area.width / columns);
                float size = Mathf.Min(maxSize, area.height / rows);
                float contentWidth = columns * size + (columns - 1f) * gap;
                float startX = area.x + Mathf.Max(0f, (area.width - contentWidth) * 0.5f);
                float totalHeight = rows * size + (rows - 1f) * gap;
                float startY = area.y + Mathf.Max(0f, (area.height - totalHeight) * 0.5f);
                for (int row = 0; row < board.launcherVisibleRows; ++row)
                {
                    for (int column = 0; column < board.launcherColumns; ++column)
                    {
                        int index = row * board.launcherColumns + column;
                        if (index >= launchers.Length || !launchers[index].present)
                        {
                            continue;
                        }
                        Rect slot = new Rect(startX + column * (size + gap), startY + row * (size + gap), size, size);
                        DrawLauncherSlot(widgets, launchers[index], slot, row == 0);
                    }
                }
            }
            finally
            {
                launchers.Dispose();
            }
        }

        private void DrawLauncherGadget(BoardInfoData board, SessionInfo session)
        {
            Widgets widgets = App.Widgets;
            Rect gadget = layout.Interaction;
            Rect inner = widgets.Gadget(gadget, null);
            if (layout.Portrait)
            {
                DrawPortraitLauncherRows(widgets, board, session, new Rect(inner.x, inner.y, inner.width, inner.height));
                return;
            }
            Rect physics = new Rect(inner.x, inner.yMax - LauncherBackendHeight, inner.width, LauncherBackendHeight);
            DrawBackendChoices(widgets, session, physics);

            float launcherRegionHeight = inner.height - LauncherBackendHeight - 8f;
            widgets.Label(new Rect(inner.x, inner.y, inner.width, LauncherHeaderHeight), "Launchers",
                TypeSizes.Value, Palette.Accent, TextAnchor.UpperLeft, true);
            Rect grid = new Rect(inner.x, inner.y + LauncherHeaderHeight + 6f, inner.width,
                Mathf.Max(0f, launcherRegionHeight - LauncherHeaderHeight - 6f));

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
            bool compactLayout = inner.width < TwoColumnWidth;
            float physicsHeight = 24f + AvailableBackendCount() * 38f;
            float gpuHeight = GpuChoicesHeight;
            float contentHeight = 52f + ProbeContentHeight + SpawnContentHeight +
                                  (compactLayout ? physicsHeight + gpuHeight : Mathf.Max(physicsHeight, gpuHeight)) + 48f;
            float y = 0f;
            widgets.ScrollColumn(inner, contentHeight, ref interactionScroll, content =>
            {
                y = content.y;
                bool compact = compactLayout;
                float physicsHeight = 24f + AvailableBackendCount() * 38f;
                float gpuHeight = GpuChoicesHeight;
                y += DrawReleaseAndProbe(widgets, backend, session, content, y);
                y += DrawSpawnControls(widgets, backend, content, y);
                if (compact)
                {
                    Rect physicsGroup = new Rect(content.x, y, content.width, physicsHeight);
                    DrawBackendChoices(widgets, session, physicsGroup);
                    y += physicsHeight + 12f;
                    Rect gpuGroup = new Rect(content.x, y, content.width, gpuHeight);
                    DrawGpuChoices(widgets, gpuGroup);
                    y += gpuHeight;
                }
                else
                {
                    float choiceWidth = Mathf.Min(190f, (content.width - 8f) * 0.5f);
                    Rect physicsGroup = new Rect(content.x, y, choiceWidth, physicsHeight);
                    DrawBackendChoices(widgets, session, physicsGroup);
                    Rect gpuGroup = new Rect(physicsGroup.xMax + 8f, y, content.width - choiceWidth - 8f, gpuHeight);
                    DrawGpuChoices(widgets, gpuGroup);
                    y += Mathf.Max(physicsHeight, gpuHeight);
                }
            });
        }

        private float DrawReleaseAndProbe(Widgets widgets, GameBackend backend, SessionInfo session, Rect content, float y)
        {
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
                backend?.StartProbe(probeTargetFps);
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
            return ProbeContentHeight;
        }

        private float DrawSpawnControls(Widgets widgets, GameBackend backend, Rect content, float y)
        {
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Spawn", TypeSizes.Small, Palette.Accent,
                TextAnchor.UpperLeft, true);
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
                SelectBallLimit(backend);
            }
            return SpawnContentHeight;
        }

        private void DrawBackendChoices(Widgets widgets, SessionInfo session, Rect area)
        {
            widgets.Label(new Rect(area.x, area.y, area.width, 22f), "Physics", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            float y = area.y + 24f;
            for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
            {
                if (kind == BallsNative.BackendHybrid && !BallsNative.BackendAvailable(kind))
                {
                    continue;
                }
                bool available = BallsNative.BackendAvailable(kind);
                bool starting = session.pendingBackend == kind;
                string detail = starting ? "starting..." : BallsNative.BackendTextCached(kind, available);
                if (widgets.OptionRow(new Rect(area.x, y, area.width, 34f), BallsNative.BackendName(kind),
                        session.backend == kind || starting, available, detail) && available)
                {
                    App.SwitchBackend(kind);
                }
                y += 38f;
            }
        }

        private static void DrawGpuChoices(Widgets widgets, Rect area)
        {
            widgets.Label(new Rect(area.x, area.y, area.width, 22f), "Render GPU", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            widgets.Muted(new Rect(area.x, area.y + 22f, area.width, 18f), "Automatic", TypeSizes.Small);
            widgets.Label(new Rect(area.x, area.y + 40f, area.width, 22f), "Compute GPU (Vulkan)", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            widgets.Muted(new Rect(area.x, area.y + 62f, area.width, 18f), "Automatic", TypeSizes.Small);
        }

        private int AvailableBackendCount()
        {
            int count = 0;
            for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
            {
                if (kind == BallsNative.BackendHybrid && !BallsNative.BackendAvailable(kind))
                {
                    continue;
                }
                count++;
            }
            return count;
        }

        private static bool IsAutoReleasing(SessionInfo session) => session.state == BallsNative.StateRunning;

        private void DrawPortraitDebugStrip(BoardInfoData board, SessionInfo session, SessionStatsData stats)
        {
            Widgets widgets = App.Widgets;
            Rect area = layout.Debug;
            const int columns = 4;
            float cellWidth = area.width / columns;
            string[] labels = { "FPS", "Min", "Frame", "GPU", "Balls", "Bricks", "Launch", "Backend" };
            string[] values =
            {
                $"{stats.fps:F0}", $"{stats.frameMinFps:F0}", $"{stats.frameMs:F1}", GpuText(),
                $"{stats.liveBalls:N0}", $"{board.aliveBricks:N0}", $"{session.launcherRemaining:N0}",
                BallsNative.BackendName(session.backend),
            };
            for (int index = 0; index < labels.Length; ++index)
            {
                int row = index / columns;
                int column = index % columns;
                Rect cell = new Rect(area.x + column * cellWidth, area.y + row * (PortraitDebugHeight / 2f),
                    cellWidth, PortraitDebugHeight / 2f);
                widgets.Label(new Rect(cell.x, cell.y, cell.width, 17f), labels[index], TypeSizes.Small,
                    Palette.TextMuted, TextAnchor.MiddleCenter);
                widgets.Label(new Rect(cell.x, cell.y + 16f, cell.width, cell.height - 16f), values[index],
                    TypeSizes.Label, labels[index] == "FPS" && stats.fps < 60f ? Palette.Danger : Palette.Text,
                    TextAnchor.MiddleCenter, true);
            }
        }

        private void DrawDebugGadget(BoardInfoData board, SessionInfo session, SessionStatsData stats)
        {
            Widgets widgets = App.Widgets;
            Rect gadget = layout.Debug;
            Rect inner = widgets.Gadget(gadget, null);
            GameBackend backend = SessionSystem.Current?.Backend;
            bool performance = session.mode == BallsApp.PerformanceMode;
            string device = BallsNative.BackendTextCached(session.backend, true);
            bool probeShown = backend != null && backend.ProbePhase != 0;
            bool fault = backend != null && !string.IsNullOrEmpty(backend.LastFault);
            bool collisionsWanted = App.Settings != null && App.Settings.BallCollisions;
            float contentHeight = 620f + (string.IsNullOrEmpty(device) ? 0f : 46f) + (probeShown ? 46f : 0f) +
                                  (fault ? 52f : 0f);
            widgets.ScrollColumn(inner, contentHeight, ref debugScroll, content =>
            {
                float y = content.y;
                float half = (content.width - 12f) * 0.5f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "FPS", $"{stats.fps:N0}",
                    stats.fps >= 60f ? Palette.Success : Palette.Danger);
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Min FPS (1s)", $"{stats.frameMinFps:N0}",
                    stats.frameMinFps >= 60f ? Palette.Success : Palette.Danger);
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Frame ms", $"{stats.frameMs:F2}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Physics ms", $"{stats.physicsMs:F2}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "GPU ms", GpuText());
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Backend",
                    BallsNative.BackendName(session.backend));
                y += 46f;
                if (probeShown)
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Probe",
                        $"{ProbePhaseName(backend.ProbePhase)} {backend.ProbeFps:F0}/{backend.ProbeTargetFps:F0} fps");
                    y += 46f;
                }
                if (!string.IsNullOrEmpty(device))
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Device", device);
                    y += 46f;
                }
                widgets.Readout(new Rect(content.x, y, content.width, 40f), "Ball collisions",
                    stats.collisions ? "On" : collisionsWanted ? "On (unsupported)" : "Off",
                    collisionsWanted && !stats.collisions ? Palette.Danger : (Color?)null);
                y += 52f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Objects", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 24f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Total objects",
                    $"{stats.liveBalls + (ulong)board.aliveBricks + (ulong)session.launcherRemaining:N0}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Balls", $"{stats.liveBalls:N0}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "Bricks", $"{board.aliveBricks:N0}");
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Launchers",
                    $"{session.launcherRemaining:N0}");
                y += 46f;
                widgets.Readout(new Rect(content.x, y, content.width, 40f), "Waiting to spawn", $"{stats.waitingToSpawn:N0}");
                y += 46f;
                if (performance && backend != null)
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Ball limit", BallLimitText(backend));
                    y += 46f;
                }
                y += 6f;

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
                if (fault)
                {
                    y += 8f;
                    widgets.Label(new Rect(content.x, y, content.width, 22f), "Backend fault", TypeSizes.Small,
                        Palette.Accent, TextAnchor.UpperLeft, true);
                    y += 24f;
                    widgets.Label(new Rect(content.x, y, content.width, 22f), "Fell back to CPU", TypeSizes.Small,
                        Palette.Danger, TextAnchor.UpperLeft);
                }
            });
        }

        /// <summary>Auto uses the machine's measured limit, probing first when there is none; the rest are fixed limits.</summary>
        private void SelectBallLimit(GameBackend backend)
        {
            if (backend == null)
            {
                return;
            }
            if (limitIndex != 0)
            {
                autoLimit = false;
                backend.SelectBallLimit(limitIndex - 1);
            }
            else if (backend.HasMachineLimit)
            {
                autoLimit = true;
                backend.UseMachineLimit(backend.MachineLimit);
            }
            else
            {
                autoLimit = true;
                backend.StartProbe(probeTargetFps);
            }
        }

        /// <summary>The Auto option shows the number it resolved to; a fixed option shows its own limit.</summary>
        private string BallLimitText(GameBackend backend)
        {
            if (!autoLimit)
            {
                return backend.BallLimit == ulong.MaxValue ? "Max (2^64-1)" : $"{backend.BallLimit:N0}";
            }
            return backend.HasMachineLimit ? $"Auto {backend.MachineLimit:N0}" : "Auto (probe needed)";
        }

        private static string ProbePhaseName(int phase) => phase switch
        {
            1 => "settling",
            2 => "measuring",
            3 => "confirming",
            4 => "done",
            _ => "idle",
        };

        private string GpuText() => App.GpuMs > 0.0 ? $"{App.GpuMs:F2}" : "n/a";

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
                App.StartNextLevel();
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
