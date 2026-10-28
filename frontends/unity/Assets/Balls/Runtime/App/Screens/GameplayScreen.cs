using System;
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
        private const float ReleaseHeight = 62f;
        private const float LauncherTapSlop = 8f;
        private const float LauncherHoverBrighten = 0.25f;

        private Vector2 launcherPressStart;
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
        private bool advancedOpen;
        private float performanceContentHeight = 600f;
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
            advancedOpen = App.Settings != null && App.Settings.AdvancedSettings;
            probeRecorded = false;
            ReapplySavedLimit(backend);
        }

        /// <summary>A capacity probe result saved for this backend becomes the Auto ball limit of a new Performance game.</summary>
        private void ReapplySavedLimit(GameBackend backend)
        {
            if (backend.Mode != BallsApp.PerformanceMode)
            {
                return;
            }
            // Every Performance game starts on Auto, as in SDL: the remembered limit when it was measured at this
            // target, otherwise a fresh probe.
            limitIndex = 0;
            autoLimit = true;
            string name = BallsNative.BackendName(backend.BackendKind);
            if (App.Profile.TryLimit(backend.BackendKind, out ulong balls, out double fps) && Mathf.RoundToInt((float)fps) == probeTargetFps)
            {
                backend.UseMachineLimit(balls);
                App.Toast($"{name} limit {balls:N0} balls @{fps:F0} FPS");
            }
            else
            {
                backend.StartProbe(probeTargetFps);
            }
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
            App.Profile.SetLimit(backend.BackendKind, backend.ProbeBest, backend.ProbeTargetFps);
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

        /// <summary>The control API drives the same pause the header button does.</summary>
        public void SetPaused(bool value) => paused = value;

        public int ProbeTargetFps => probeTargetFps;

        /// <summary>The control API's /probe: the target the stepper would set, then a probe at it.</summary>
        public void ControlProbe(int targetFps)
        {
            probeTargetFps = targetFps;
            SessionSystem.Current?.Backend?.StartProbe(targetFps);
        }

        /// <summary>Opens or closes the control gadget's Advanced Settings section, as its row does.</summary>
        public void SetAdvancedOpen(bool open) => advancedOpen = open;

        /// <summary>Scrolls the control gadget to its end, so a conformance shot can cover Advanced Settings.</summary>
        public void ScrollControlsToEnd() => interactionScroll.y = 100000f;

        /// <summary>Drops the cached layout, so a Settings change (orientation) is applied on the next frame.</summary>
        public void RefreshLayout() => hasLayout = false;

        /// <summary>The control API's handle on the gadget's Spawn controls, which update the rows as a click does.</summary>
        public void ControlSpawnRate(int index)
        {
            spawnRateIndex = Mathf.Clamp(index, 0, spawnRateOptions.Length - 1);
            SessionSystem.Current?.Backend?.SelectSpawnRate(spawnRateIndex);
        }

        public void ControlMultiplier(int index)
        {
            multiplierIndex = Mathf.Clamp(index, 0, multiplierOptions.Length - 1);
            SessionSystem.Current?.Backend?.SelectBallMultiplier(multiplierIndex);
        }

        public void ControlBallLimit(int index)
        {
            limitIndex = Mathf.Clamp(index, 0, limitOptions.Length - 1);
            SelectBallLimit(SessionSystem.Current?.Backend);
        }

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
            Debug.Log($"balls: layout for {UnityEngine.Screen.width}x{UnityEngine.Screen.height} px, units {width:0}x{height:0} " +
                      $"scale {App.Theme.Scale:0.###}: board {layout.BoardRect} cell {layout.CellSize:0.#} " +
                      $"main {layout.Main} inter {layout.Interaction} debug {layout.Debug}");
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

        /// <summary>
        /// The game's header: the whole strip is the Pause button. Hovered or pressed it takes the hover or pressed fill and
        /// shows only the label; otherwise it shows the title (and a disk notice under it), scrolling sideways when the
        /// text is wider than the strip, resting 1.5 s at each end at 40 units a second.
        /// </summary>
        private void DrawHeader(SessionInfo session, BoardInfoData board)
        {
            UiTheme theme = App.Theme;
            Rect header = layout.Header;
            Rect pixels = theme.Units(header.x, header.y, header.width, header.height);
            Vector2 mouse = Event.current.mousePosition;
            bool hovered = pixels.Contains(mouse);
            bool pressed = hovered && (Event.current.type == EventType.MouseDown || Event.current.type == EventType.MouseDrag);
            if (hovered)
            {
                theme.Fill(header, pressed ? Palette.ControlPressed : Palette.ControlHover);
            }

            string modeName = session.mode == BallsApp.PerformanceMode ? "Performance" : "Normal";
            string title = $"{modeName} · {App.LevelName}";
            string notice = LevelWatcher.Notice(watcher.Poll());

            if (hovered)
            {
                GUI.Label(pixels, paused ? "Resume" : "Pause",
                    theme.TextStyle(TypeSizes.Header, Palette.Text, false, TextAnchor.MiddleCenter));
            }
            else
            {
                // 14 units in from the left, the title over the notice when the level files changed under the game.
                Rect text = new Rect(header.x + 14f, header.y, header.width - 28f, header.height);
                float titleHeight = notice == null ? text.height : text.height * 0.58f;
                DrawScrollingText(new Rect(text.x, text.y, text.width, titleHeight), title,
                    theme.TextStyle(notice == null ? TypeSizes.Header : TypeSizes.Value, Palette.Text, false,
                        TextAnchor.MiddleLeft, true));
                if (notice != null)
                {
                    DrawScrollingText(new Rect(text.x, text.y + titleHeight, text.width, text.height - titleHeight),
                        notice, theme.TextStyle(TypeSizes.Tiny, Palette.Accent, false, TextAnchor.MiddleLeft, true));
                }
            }

            if (GUI.Button(pixels, GUIContent.none, GUIStyle.none))
            {
                paused = !paused;
            }
        }

        /// <summary>
        /// Draws text clipped to its box, sliding sideways when it does not fit: 1.5 s at rest, 40 units a second to
        /// show the end, 1.5 s there, then back.
        /// </summary>
        private void DrawScrollingText(Rect box, string text, GUIStyle style)
        {
            Rect pixels = App.Theme.Units(box.x, box.y, box.width, box.height);
            float textWidth = style.CalcSize(new GUIContent(text)).x;
            float overflow = (textWidth - pixels.width) / App.Theme.Scale;
            float offset = 0f;
            if (overflow > 0.5f)
            {
                const float rest = 1.5f;
                const float speed = 40f;
                float travel = overflow / speed;
                float cycle = (rest + travel) * 2f;
                float phase = Time.unscaledTime % cycle;
                if (phase < rest)
                {
                    offset = 0f;
                }
                else if (phase < rest + travel)
                {
                    offset = (phase - rest) * speed;
                }
                else if (phase < rest + travel + rest)
                {
                    offset = overflow;
                }
                else
                {
                    offset = overflow - (phase - rest - travel - rest) * speed;
                }
            }
            GUI.BeginClip(pixels);
            try
            {
                GUI.Label(new Rect(-offset * App.Theme.Scale, 0f, pixels.width + overflow * App.Theme.Scale, pixels.height),
                    text, style);
            }
            finally
            {
                GUI.EndClip();
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
                float fit = Mathf.Min((area.width - (columns - 1f) * gap) / columns,
                    (area.height - (rows - 1f) * gap) / rows);
                float preferred = Mathf.Clamp(area.width * 0.19f, 120f, 240f);
                float size = Mathf.Min(preferred, fit);
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
                        DrawLauncherSlot(widgets, launchers[index], slot, row == 0, session);
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
            // Under the launchers, the collapsible Advanced Settings with the Physics list and Ball collisions.
            DrawAdvancedSection(widgets, session, inner);
            float launcherRegionHeight = inner.height - 8f - AdvancedSectionHeight();
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
                        DrawLauncherSlot(widgets, launcher, slot, row == 0, session);
                    }
                }
            }
            finally
            {
                launchers.Dispose();
            }
        }

        private void DrawLauncherSlot(Widgets widgets, LauncherElement launcher, Rect slot, bool front,
            SessionInfo session)
        {
            Rect pixels = App.Theme.Units(slot.x, slot.y, slot.width, slot.height);
            Event current = Event.current;
            if (current.type == EventType.MouseDown)
            {
                launcherPressStart = current.mousePosition;
            }
            Color color = Palette.Balls[Mathf.Clamp(launcher.color, 0, Palette.Balls.Length - 1)];
            // Hover brightens only while the game runs unpaused; touch has no hover.
            if (session.state == BallsNative.StateRunning && !paused && current.type == EventType.Repaint &&
                pixels.Contains(current.mousePosition) && Input.touchCount == 0)
            {
                color = Color.Lerp(color, Color.white, LauncherHoverBrighten);
            }
            float fade = front ? 1f : 0.55f;
            App.Theme.Frame(slot, UiTheme.WithAlpha(color, fade), UiTheme.WithAlpha(Color.white, 0.10f));
            widgets.Label(new Rect(slot.x, slot.y + slot.height * 0.5f - 12f, slot.width, 24f),
                launcher.ballCount.ToString(), TypeSizes.Label, Palette.Background, TextAnchor.MiddleCenter, true);

            if (GUI.Button(pixels, GUIContent.none, GUIStyle.none))
            {
                // Portrait releases on the tap's release; a press that travelled over 8 units was a drag, not a tap.
                float travel = (current.mousePosition - launcherPressStart).magnitude / App.Theme.Scale;
                if (!layout.Portrait || travel <= LauncherTapSlop)
                {
                    BallsInput.ClickLauncher(launcher.column, launcher.row);
                }
            }
        }

        private void DrawPerformanceGadget(BoardInfoData board, SessionInfo session)
        {
            Widgets widgets = App.Widgets;
            GameBackend backend = SessionSystem.Current?.Backend;
            Rect gadget = layout.Interaction;
            Rect inner = widgets.Gadget(gadget, null);
            bool compactLayout = inner.width < TwoColumnWidth;
            float y = 0f;
            widgets.ScrollColumn(inner, performanceContentHeight, ref interactionScroll, content =>
            {
                y = content.y;
                bool compact = compactLayout;
                float physicsHeight = 24f + AvailableBackendCount() * 38f;
                float gpuHeight = GpuChoicesHeight();
                y += DrawRelease(session, content, y);
                y += DrawSpawnControls(widgets, backend, content, y);
                y += DrawAdvancedToggle(widgets, content, y);
                if (!advancedOpen)
                {
                    performanceContentHeight = y - content.y + 24f;
                    return;
                }
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
                // Devices, then Ball collisions last, both across the full width.
                y = SharedControls.DrawDevicePicks(App, widgets, content, y + 12f, session.backend, SwitchToPicks);
                y = SharedControls.DrawBallCollisions(App, widgets, content, y + 4f);
                performanceContentHeight = y - content.y + 24f;
            });
        }

        /// <summary>The game's Release row: Start (1 click), then a disabled Auto releasing - N launchers left.</summary>
        private float DrawRelease(SessionInfo session, Rect content, float y)
        {
            Widgets widgets = App.Widgets;
            var row = new Rect(content.x, y, content.width, 52f);
            if (session.launcherRemaining > 0 && !IsAutoReleasing(session))
            {
                if (widgets.AccentButton(row, "Start (1 click)", true, TypeSizes.Label))
                {
                    BallsInput.RequestAutoRelease();
                }
            }
            else
            {
                widgets.Button(row, $"Auto releasing - {session.launcherRemaining:N0} launchers left", TypeSizes.Label,
                    false);
            }
            return ReleaseHeight;
        }

        private static string ProbeStatus(GameBackend backend)
        {
            if (backend == null)
            {
                return string.Empty;
            }
            if (backend.ProbeActive)
            {
                return $"Probing... {backend.ProbeBest:N0}, {backend.ProbeFps:F0}";
            }
            return backend.ProbeBest > 0
                ? $"Max @{backend.ProbeTargetFps:F0} FPS: {backend.ProbeBest:N0}"
                : $"Target {backend.ProbeTargetFps:F0} FPS";
        }

        /// <summary>Applies picks changed in the gadget to this game only, as the game's in-game Devices rows do.</summary>
        private void SwitchToPicks(List<BallsNative.Pick> picks)
        {
            App.SwitchBackend(BallsNative.KindForPicks(picks));
            App.Toast($"Physics on {BallsNative.PicksLabel(picks)}");
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
            y += 42f;
            float height = 24f + 3 * 42f;
            if (limitIndex == 0)
            {
                // Auto: the probe's target sits under the dropdown, with no separate probe button; changing the
                // target probes again.
                if (widgets.Stepper(new Rect(content.x, y, content.width, 36f), "Target FPS", ref probeTargetFps, 10,
                        1000, out _))
                {
                    backend?.StartProbe(probeTargetFps);
                }
                widgets.Muted(new Rect(content.x, y + 40f, content.width, 22f), ProbeStatus(backend), TypeSizes.Small);
                height += 70f;
            }
            return height;
        }

        /// <summary>The game's collapsible Advanced Settings header: a row with a chevron, remembered in the settings.</summary>
        private float DrawAdvancedToggle(Widgets widgets, Rect content, float y)
        {
            string label = advancedOpen ? "Advanced Settings  ^" : "Advanced Settings  v";
            if (widgets.Button(new Rect(content.x, y, content.width, 34f), label, TypeSizes.Small))
            {
                advancedOpen = !advancedOpen;
                if (App.Settings != null)
                {
                    App.Settings.AdvancedSettings = advancedOpen;
                    App.SaveSettings();
                }
            }
            return 38f;
        }

        /// <summary>Draws the toggle at the bottom of the launcher gadget, and the Physics group under it when open.</summary>
        private float AdvancedSectionHeight() => advancedOpen
            ? 58f + AvailableBackendCount() * 38f + 8f + SharedControls.BallCollisionsHeight()
            : 34f;

        private void DrawAdvancedSection(Widgets widgets, SessionInfo session, Rect inner)
        {
            float height = AdvancedSectionHeight();
            Rect area = new Rect(inner.x, inner.yMax - height, inner.width, height);
            float y = area.y;
            y += DrawAdvancedToggle(widgets, area, y);
            if (!advancedOpen)
            {
                return;
            }
            Rect physicsGroup = new Rect(area.x, y + 6f, area.width, 24f + AvailableBackendCount() * 38f);
            DrawBackendChoices(widgets, session, physicsGroup);
            SharedControls.DrawBallCollisions(App, widgets, area, physicsGroup.yMax + 8f);
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

        /// <summary>
        /// The GPU rows: the render adapter is Unity's, so its row names the adapter in use; the compute adapter is the
        /// Vulkan plugin's and is chosen here, like the SDL front end's list.
        /// </summary>
        private void DrawGpuChoices(Widgets widgets, Rect area)
        {
            widgets.Label(new Rect(area.x, area.y, area.width, 22f), "Render GPU", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            string renderGpu = widgets.Ellipsize($"{SystemInfo.graphicsDeviceName} (Automatic)", TypeSizes.Small,
                area.width - 4f);
            widgets.Muted(new Rect(area.x, area.y + 22f, area.width, 18f), renderGpu, TypeSizes.Small);
            float y = area.y + 44f;
            widgets.Label(new Rect(area.x, y, area.width, 22f), "Compute GPU (Vulkan)", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            y += 24f;
            string current = BallsNative.ComputeGpu;
            bool automatic = current.Length == 0 || current.Equals("auto", StringComparison.OrdinalIgnoreCase);
            if (widgets.OptionRow(new Rect(area.x, y, area.width, 34f), "Automatic", automatic) && !automatic)
            {
                ApplyComputeGpu("auto", "Automatic");
            }
            y += 38f;
            foreach (BallsNative.PhysicsDevice device in BallsNative.Devices)
            {
                if (!device.vulkan)
                {
                    continue;
                }
                bool selected = !automatic && current.Equals(device.id, StringComparison.OrdinalIgnoreCase);
                string detail = device.memoryBytes > 0 ? $"{device.memoryBytes / (1024UL * 1024UL * 1024UL)} GB" : null;
                if (widgets.OptionRow(new Rect(area.x, y, area.width, 34f), device.name, selected, true, detail))
                {
                    ApplyComputeGpu(device.id, device.name);
                }
                y += 38f;
            }
        }

        private void ApplyComputeGpu(string preference, string label)
        {
            if (BallsNative.SetComputeGpu(preference))
            {
                App.Toast($"Compute GPU: {label}");
            }
            else
            {
                App.Toast($"No {label} for the Vulkan backend", true);
            }
        }

        private int VulkanDeviceCount()
        {
            int count = 0;
            foreach (BallsNative.PhysicsDevice device in BallsNative.Devices)
            {
                if (device.vulkan)
                {
                    ++count;
                }
            }
            return count;
        }

        /// <summary>The two GPU headings, plus Automatic and every Vulkan GPU the plugins listed.</summary>
        private float GpuChoicesHeight() => 44f + 24f + 38f * (1 + VulkanDeviceCount());

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
            // One row of eight, label above value, inside the core's 48-high strip: two rows cannot fit at the
            // narrowest portrait width without the labels running into the values.
            string[] labels = advancedOpen
                ? new[] { "FPS", "Min", "Frame", "GPU", "Balls", "Bricks", "Launch", "Backend" }
                : new[] { "FPS", "Min", "Frame", "GPU", "Balls", "Bricks", "Launch" };
            string[] values = advancedOpen
                ? new[]
                {
                    $"{stats.fps:F0}", $"{stats.frameMinFps:F0}", $"{stats.frameMs:F1}", GpuText(),
                    $"{stats.liveBalls:N0}", $"{board.aliveBricks:N0}", $"{session.launcherRemaining:N0}",
                    BallsNative.BackendName(session.backend),
                }
                : new[]
                {
                    $"{stats.fps:F0}", $"{stats.frameMinFps:F0}", $"{stats.frameMs:F1}", GpuText(),
                    $"{stats.liveBalls:N0}", $"{board.aliveBricks:N0}", $"{session.launcherRemaining:N0}",
                };
            float cellWidth = area.width / labels.Length;
            for (int index = 0; index < labels.Length; ++index)
            {
                Rect cell = new Rect(area.x + index * cellWidth, area.y, cellWidth, area.height);
                widgets.Label(new Rect(cell.x, cell.y, cell.width, 14f), labels[index], TypeSizes.Tiny,
                    Palette.TextMuted, TextAnchor.MiddleCenter);
                widgets.Label(new Rect(cell.x, cell.y + 13f, cell.width, cell.height - 13f), values[index],
                    TypeSizes.Value, labels[index] == "FPS" && stats.fps < 60f ? Palette.Danger : Palette.Text,
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
            float contentHeight = 666f + (string.IsNullOrEmpty(device) ? 0f : 46f) + (probeShown ? 46f : 0f) +
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
                widgets.Readout(new Rect(content.x, y, half, 40f), "Physics FPS", $"{stats.physicsFps:N0}",
                    stats.physicsFps >= 60.0 ? Palette.Success : Palette.Danger);
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "GPU ms", GpuText());
                y += 46f;
                if (advancedOpen)
                {
                    // The design leaves the backend rows out while the control gadget's section is closed.
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Backend",
                        BallsNative.BackendName(session.backend));
                    y += 46f;
                }
                if (probeShown)
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Probe",
                        $"{ProbePhaseName(backend.ProbePhase)} {backend.ProbeFps:F0}/{backend.ProbeTargetFps:F0} fps");
                    y += 46f;
                }
                if (advancedOpen && !string.IsNullOrEmpty(device))
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Device",
                        widgets.Ellipsize(device, TypeSizes.Value, content.width));
                    y += 46f;
                }
                if (advancedOpen)
                {
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Renderer",
                        widgets.Ellipsize(BallsApp.RendererText, TypeSizes.Value, content.width));
                    y += 46f;
                    widgets.Readout(new Rect(content.x, y, content.width, 40f), "Ball collisions",
                        stats.collisions ? "On" : collisionsWanted ? "On (unsupported)" : "Off",
                        collisionsWanted && !stats.collisions ? Palette.Danger : (Color?)null);
                    y += 52f;
                }

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
