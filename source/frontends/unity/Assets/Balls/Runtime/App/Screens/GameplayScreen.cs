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

    public sealed class GameplayScreen : Screen
    {

        private const double KeepingUpTickRate = 120.0 * 0.95;

        private const float LauncherMaxSize = 110f;
        private const float LauncherGapShare = 0.18f;
        private const float GadgetPadding = 12f;
        private const float LauncherHeaderHeight = 24f;
        private const float BrickLabelShare = 0.55f;
        private const float MinimumLabelPixels = 9f;
        private static readonly string[] HitLabels = new string[128];

        private static string HitLabel(int hits)
        {
            if (hits < 0 || hits >= HitLabels.Length)
            {
                return hits.ToString();
            }
            return HitLabels[hits] ??= hits.ToString();
        }
        private const float TwoColumnWidth = 2f * 220f + 16f;
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

        private readonly List<CellElement> labelCells = new();
        public bool Paused => App.IsPaused;
        public void SetPaused(bool value)
        {
            if (value != App.IsPaused)
            {
                App.ActivateFlow(BallsNative.FlowTogglePause);
            }
        }
        private int probeTargetFps = 60;
        private int spawnRateIndex;
        private int limitIndex;
        private bool advancedOpen;
        private float performanceContentHeight = 600f;
        private bool probeRecorded;
        private GameBackend syncedBackend;
        private Vector2 interactionScroll;
        private Vector2 debugScroll;
        private readonly string[] spawnRateOptions = { "10", "50", "200", "1000", "2000", "10k", "50k", "200k", "1M", "Max" };
        private readonly string[] limitOptions = { "Level", "Auto", "Max" };

        public void BeginSession()
        {
            System.Array.Clear(brickMaxHits, 0, brickMaxHits.Length);
            hasLayout = false;
            layoutOrientation = -1;
            lastLayoutWidth = -1f;
            lastLayoutHeight = -1f;
            interactionScroll = Vector2.zero;
            debugScroll = Vector2.zero;
            probeTargetFps = 60;
            syncedBackend = null;
        }

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
                if (backend.BallLimitIndex == 1)
                {
                    backend.SelectBallLimit(1);
                }
            }
            else if (outcome == 2)
            {
                App.Toast(BallsNative.MenuMessage(BallsNative.MenuTextBackendFailed, kind, 0, 0, null));
            }
        }

        private void SyncControls()
        {
            GameBackend backend = SessionSystem.Current?.Backend;
            if (backend == null || ReferenceEquals(backend, syncedBackend))
            {
                return;
            }
            syncedBackend = backend;
            spawnRateIndex = backend.SpawnRateIndex;
            limitIndex = backend.BallLimitIndex;
            advancedOpen = App.Settings != null && App.Settings.AdvancedSettings;
            probeRecorded = false;
        }

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

            HandleSwitchOutcome();
            SyncControls();
            if (SessionSystem.Current?.Backend != null)
            {
                RecordFinishedProbe(SessionSystem.Current.Backend);
            }
            EnsureLayout(board);

            RefitCamera(board);

            theme.Frame(layout.Main, UiTheme.Clear, Palette.Border);
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
        }

        public override void DrawOverlay()
        {
            int overlay = App.FlowOverlay;
            if (overlay == BallsNative.OverlaySettings)
            {
                App.Get<SettingsScreen>().DrawAsOverlay(() => App.ActivateFlow(BallsNative.FlowBack));
                return;
            }
            if (overlay == BallsNative.OverlayWin)
            {
                DrawWinOverlay();
                return;
            }
            if (overlay == BallsNative.OverlayPause)
            {
                DrawPauseOverlay();
            }
        }

        public override void HandleEscape()
        {
            App.EscapeFlow();
        }

        public LevelWatcher.State WatchState => (LevelWatcher.State)App.FlowDiskState;

        public int ProbeTargetFps => probeTargetFps;

        public void ControlProbe(int targetFps)
        {
            probeTargetFps = targetFps;
            SessionSystem.Current?.Backend?.StartProbe(targetFps);
        }

        public void SetAdvancedOpen(bool open) => advancedOpen = open;

        public void ScrollControlsToEnd() => interactionScroll.y = 100000f;

        public void RefreshLayout() => hasLayout = false;

        public void ControlSpawnRate(int index)
        {
            spawnRateIndex = Mathf.Clamp(index, 0, spawnRateOptions.Length - 1);
            SessionSystem.Current?.Backend?.SelectSpawnRate(spawnRateIndex);
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

            float windowCells = theme.UnitsHeight / cellSize;
            camera.orthographic = true;
            camera.orthographicSize = windowCells * 0.5f;
            camera.rect = new Rect(0f, 0f, 1f, 1f);

            camera.ResetProjectionMatrix();
            camera.projectionMatrix = camera.projectionMatrix * Matrix4x4.Scale(new Vector3(1f, -1f, 1f));
            camera.transform.position = new Vector3(
                theme.UnitsWidth / (2f * cellSize) - worldBoardRect.x / cellSize,
                (theme.UnitsHeight * 0.5f - worldBoardRect.y) / cellSize,
                -10f);
            camera.transform.rotation = Quaternion.identity;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Palette.BoardBackdrop;
            camera.nearClipPlane = 0.01f;
            camera.farClipPlane = 100f;
            _ = board;
        }

        private int[] brickMaxHits = System.Array.Empty<int>();
        private int[] labelMinColumn = System.Array.Empty<int>();
        private int[] labelMaxColumn = System.Array.Empty<int>();
        private int[] labelMinRow = System.Array.Empty<int>();
        private int[] labelMaxRow = System.Array.Empty<int>();
        private int[] labelCellCount = System.Array.Empty<int>();
        private float[] labelSumColumn = System.Array.Empty<float>();
        private float[] labelSumRow = System.Array.Empty<float>();
        private int[] labelNearest = System.Array.Empty<int>();
        private float[] labelNearestDistance = System.Array.Empty<float>();

        private static void Size<T>(ref T[] array, int length)
        {
            if (array.Length < length)
            {
                array = new T[length];
            }
        }

        private void DrawBrickLabels(BoardInfoData board)
        {
            float cell = layout.CellSize;
            if (Event.current.type != EventType.Repaint || cell * BrickLabelShare * App.Theme.Scale < MinimumLabelPixels ||
                !App.TryReadCells(labelCells))
            {
                return;
            }
            int bricks = Mathf.Max(board.brickCount, 1);
            if (brickMaxHits.Length < bricks)
            {
                System.Array.Resize(ref brickMaxHits, bricks);
            }
            Size(ref labelMinColumn, bricks);
            Size(ref labelMaxColumn, bricks);
            Size(ref labelMinRow, bricks);
            Size(ref labelMaxRow, bricks);
            Size(ref labelCellCount, bricks);
            Size(ref labelSumColumn, bricks);
            Size(ref labelSumRow, bricks);
            Size(ref labelNearest, bricks);
            Size(ref labelNearestDistance, bricks);
            System.Array.Clear(labelCellCount, 0, bricks);
            for (int index = 0; index < labelCells.Count; ++index)
            {
                CellElement brick = labelCells[index];
                if (brick.brick < 0 || brick.brick >= bricks || brick.hitsLeft <= 0)
                {
                    continue;
                }
                int id = brick.brick;
                brickMaxHits[id] = Mathf.Max(brickMaxHits[id], brick.hitsLeft);
                int column = index % board.paddedWidth - 1;
                int row = index / board.paddedWidth - 1;
                if (labelCellCount[id]++ == 0)
                {
                    labelMinColumn[id] = labelMaxColumn[id] = column;
                    labelMinRow[id] = labelMaxRow[id] = row;
                    labelSumColumn[id] = labelSumRow[id] = 0f;
                    labelNearestDistance[id] = float.MaxValue;
                }
                labelMinColumn[id] = Mathf.Min(labelMinColumn[id], column);
                labelMaxColumn[id] = Mathf.Max(labelMaxColumn[id], column);
                labelMinRow[id] = Mathf.Min(labelMinRow[id], row);
                labelMaxRow[id] = Mathf.Max(labelMaxRow[id], row);
                labelSumColumn[id] += column + 0.5f;
                labelSumRow[id] += row + 0.5f;
            }
            for (int index = 0; index < labelCells.Count; ++index)
            {
                CellElement brick = labelCells[index];
                if (brick.brick < 0 || brick.brick >= bricks || brick.hitsLeft <= 0)
                {
                    continue;
                }
                int id = brick.brick;
                float dx = index % board.paddedWidth - 1 + 0.5f - labelSumColumn[id] / labelCellCount[id];
                float dy = index / board.paddedWidth - 1 + 0.5f - labelSumRow[id] / labelCellCount[id];
                if (dx * dx + dy * dy < labelNearestDistance[id])
                {
                    labelNearestDistance[id] = dx * dx + dy * dy;
                    labelNearest[id] = index;
                }
            }
            Rect boardRect = layout.BoardRect;
            for (int id = 0; id < bricks; ++id)
            {
                if (labelCellCount[id] == 0 || brickMaxHits[id] <= 1)
                {
                    continue;
                }
                CellElement brick = labelCells[labelNearest[id]];
                int width = labelMaxColumn[id] - labelMinColumn[id] + 1;
                int height = labelMaxRow[id] - labelMinRow[id] + 1;
                Rect area = width * height == labelCellCount[id]
                    ? new Rect(boardRect.x + labelMinColumn[id] * cell, boardRect.y + (labelMinRow[id] + brick.rise) * cell,
                        width * cell, height * cell)
                    : new Rect(boardRect.x + (labelNearest[id] % board.paddedWidth - 1) * cell,
                        boardRect.y + (labelNearest[id] / board.paddedWidth - 1 + brick.rise) * cell, cell, cell);
                string label = HitLabel(brick.hitsLeft);
                float size = Mathf.Min(area.width, area.height) * BrickLabelShare;
                GUIStyle probe = App.Theme.TextStyle(Mathf.Max(1, Mathf.RoundToInt(size)), Color.white, false,
                    TextAnchor.MiddleCenter);
                float labelWidth = probe.CalcSize(new GUIContent(label)).x / App.Theme.Scale;
                if (labelWidth > area.width * 0.9f)
                {
                    size *= area.width * 0.9f / labelWidth;
                }
                if (size * App.Theme.Scale < MinimumLabelPixels)
                {
                    continue;
                }
                int fontSize = Mathf.RoundToInt(size);
                Color fill = Palette.Balls[Mathf.Clamp(brick.color, 0, Palette.Balls.Length - 1)];
                GUI.Label(App.Theme.Units(area.x + 1.5f, area.y + 1.5f, area.width, area.height), label,
                    App.Theme.TextStyle(fontSize, new Color(fill.r * 0.4f, fill.g * 0.4f, fill.b * 0.4f, 1f), false,
                        TextAnchor.MiddleCenter));
                GUI.Label(App.Theme.Units(area.x, area.y, area.width, area.height), label,
                    App.Theme.TextStyle(fontSize, Palette.TextStrong, false, TextAnchor.MiddleCenter));
            }
        }

        private void DrawHeader(SessionInfo session, BoardInfoData board)
        {
            UiTheme theme = App.Theme;
            Rect header = layout.Header;
            Rect pixels = theme.Units(header.x, header.y, header.width, header.height);
            Vector2 mouse = Event.current.mousePosition;
            bool hovered = pixels.Contains(mouse);
            bool pressed = hovered && (Event.current.type == EventType.MouseDown || Event.current.type == EventType.MouseDrag);
            theme.Frame(header, pressed ? Palette.ControlPressed : hovered ? Palette.ControlHover : Palette.Panel,
                Palette.Border);

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextGameTitle);
            if (string.IsNullOrEmpty(title))
            {
                string modeName = session.mode == BallsApp.PerformanceMode ? "Performance" : "Normal";
                title = $"{modeName} · {App.LevelName}";
            }
            string notice = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextDiskNotice);
            if (string.IsNullOrEmpty(notice))
            {
                notice = null;
            }

            if (hovered)
            {
                GUI.Label(pixels, App.IsPaused ? "Resume" : "Pause",
                    theme.TextStyle(TypeSizes.Header, Palette.Text, false, TextAnchor.MiddleCenter));
            }
            else
            {

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
                App.ActivateFlow(BallsNative.FlowTogglePause);
            }
        }

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

            if (session.state == BallsNative.StateRunning && !Paused && current.type == EventType.Repaint &&
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
                float physicsHeight = 24f + BackendChoiceCount() * 38f;
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

                y = SharedControls.DrawDevicePicks(App, widgets, content, y + 12f, session.backend, SwitchToPicks);
                y = SharedControls.DrawBallCollisions(App, widgets, content, y + 4f);
                performanceContentHeight = y - content.y + 24f;
            });
        }

        private const float GroupLabelHeight = 26f;
        private const float GroupGap = 14f;

        private float DrawRelease(SessionInfo session, Rect content, float y)
        {
            Widgets widgets = App.Widgets;
            widgets.GroupLabel(new Rect(content.x, y, content.width, GroupLabelHeight), "Release");
            var row = new Rect(content.x, y + GroupLabelHeight, content.width, Widgets.RowHeight);

            System.IntPtr handle = SessionSystem.Current != null ? SessionSystem.Current.SessionHandle : System.IntPtr.Zero;
            List<NativeUiOption> release = handle != System.IntPtr.Zero
                ? BallsNative.MenuOptions(BallsNative.MenuRelease, 0, handle)
                : null;
            bool canStart = release != null && release.Count > 0
                ? release[0].enabled != 0
                : session.launcherRemaining > 0 && !IsAutoReleasing(session);
            string label = release != null && release.Count > 0 ? release[0].Label
                : canStart ? "Start (1 click)" : "Auto releasing";
            if (widgets.Button(row, label, TypeSizes.Label, canStart) && canStart)
            {
                BallsInput.RequestAutoRelease();
            }
            return GroupLabelHeight + Widgets.RowHeight + GroupGap;
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

        private void SwitchToPicks(List<BallsNative.Pick> picks)
        {
            App.SwitchBackend(BallsNative.KindForPicks(picks));
            App.Toast(BallsNative.MenuPhysicsOn(picks));
        }

        private float DrawSpawnControls(Widgets widgets, GameBackend backend, Rect content, float y)
        {
            System.IntPtr session = SessionSystem.Current != null ? SessionSystem.Current.SessionHandle : System.IntPtr.Zero;
            string[] spawnRateOptions = this.spawnRateOptions;
            string[] limitOptions = this.limitOptions;
            if (session != System.IntPtr.Zero)
            {
                var spawnOpts = BallsNative.MenuOptions(BallsNative.MenuSpawnRate, spawnRateIndex, session);
                if (spawnOpts.Count > 0)
                {
                    spawnRateOptions = new string[spawnOpts.Count];
                    for (int i = 0; i < spawnOpts.Count; ++i) spawnRateOptions[i] = spawnOpts[i].Label;
                }
                var limitOpts = BallsNative.MenuOptions(BallsNative.MenuBallLimit, limitIndex, session);
                if (limitOpts.Count > 0)
                {
                    limitOptions = new string[limitOpts.Count];
                    for (int i = 0; i < limitOpts.Count; ++i) limitOptions[i] = limitOpts[i].Label;
                }
            }
            float top = y;
            widgets.GroupLabel(new Rect(content.x, y, content.width, GroupLabelHeight), "Spawn / sec");
            y += GroupLabelHeight;
            if (widgets.Dropdown("spawnrate", new Rect(content.x, y, content.width, Widgets.RowHeight), spawnRateOptions,
                    ref spawnRateIndex))
            {
                backend?.SelectSpawnRate(spawnRateIndex);
            }
            y += widgets.DropdownHeight("spawnrate", spawnRateOptions.Length) + GroupGap;
            widgets.GroupLabel(new Rect(content.x, y, content.width, GroupLabelHeight), "Ball limit");
            y += GroupLabelHeight;
            if (widgets.Dropdown("balllimit", new Rect(content.x, y, content.width, Widgets.RowHeight), limitOptions,
                    ref limitIndex))
            {
                SelectBallLimit(backend);
            }
            y += widgets.DropdownHeight("balllimit", limitOptions.Length) + GroupGap;
            return y - top;
        }

        private float DrawAdvancedToggle(Widgets widgets, Rect content, float y)
        {
            if (widgets.CollapsingHeader(new Rect(content.x, y, content.width, 40f), "Advanced Settings", advancedOpen))
            {
                advancedOpen = !advancedOpen;
                if (App.Settings != null)
                {
                    App.Settings.AdvancedSettings = advancedOpen;
                    App.SaveSettings();
                }
            }
            return 40f + GroupGap;
        }

        private float AdvancedSectionHeight() => advancedOpen
            ? 40f + GroupGap + 24f + BackendChoiceCount() * 38f + 8f + SharedControls.BallCollisionsHeight()
            : 40f;

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
            IntPtr sessionHandle = SessionSystem.Current != null ? SessionSystem.Current.SessionHandle : IntPtr.Zero;
            if (sessionHandle != IntPtr.Zero)
            {
                List<NativeUiOption> options = BallsNative.MenuOptions(BallsNative.MenuGameplayBackends, 0, sessionHandle);
                for (int i = 0; i < options.Count; ++i)
                {
                    NativeUiOption opt = options[i];
                    int kind = opt.kind;
                    bool available = opt.enabled != 0;
                    string label = opt.Label;
                    string detail = string.IsNullOrEmpty(opt.Detail) ? null : opt.Detail;
                    bool selected = opt.selected != 0;
                    if (widgets.OptionRow(new Rect(area.x, y, area.width, 34f), label, selected, available, detail) && available)
                    {
                        App.SwitchBackend(kind);
                    }
                    y += 38f;
                }
            }
            else
            {
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

            bool burstAvailable = true;
            bool burstStarting = session.pendingBackend == Balls.Physics.Burst.BurstGameBackend.BackendBurst;
            string burstDetail = burstStarting ? "starting..." : null;
            if (widgets.OptionRow(new Rect(area.x, y, area.width, 34f), "Burst",
                    session.backend == Balls.Physics.Burst.BurstGameBackend.BackendBurst || burstStarting, burstAvailable, burstDetail) && burstAvailable)
            {
                App.SwitchBackend(Balls.Physics.Burst.BurstGameBackend.BackendBurst);
            }
            y += 38f;
        }

        private int BackendChoiceCount()
        {
            IntPtr sessionHandle = SessionSystem.Current != null ? SessionSystem.Current.SessionHandle : IntPtr.Zero;
            if (sessionHandle != IntPtr.Zero)
            {
                return BallsNative.MenuOptions(BallsNative.MenuGameplayBackends, 0, sessionHandle).Count + 1;
            }
            return AvailableBackendCount();
        }

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
                App.Toast(BallsNative.MenuMessage(BallsNative.MenuTextComputeGpu, 0, 0, 0, label));
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
            IntPtr sessionHandle = SessionSystem.Current != null ? SessionSystem.Current.SessionHandle : IntPtr.Zero;
            if (sessionHandle == IntPtr.Zero)
            {

                DrawDebugGadgetBurst(board, session, stats);
                return;
            }
            Widgets widgets = App.Widgets;
            Rect gadget = layout.Debug;
            Rect inner = widgets.Gadget(gadget, null);
            IntPtr timingsHandle = SessionSystem.Current != null ? SessionSystem.Current.FrameTimingsHandle : IntPtr.Zero;
            bool showBackend = advancedOpen;
            List<NativeDebugRow> rows = BallsNative.MenuDebugRows(sessionHandle, timingsHandle, showBackend ? 1 : 0, BallsApp.RendererText);

            const float padding = 16f;
            const float lineHeight = 26f;
            const float labelHeight = 22f;
            float contentHeight = padding;
            foreach (NativeDebugRow r in rows)
            {
                if (r.kind == 0) contentHeight += 6f + lineHeight + 4f;
                else if (r.kind == 2) contentHeight += lineHeight;
                else contentHeight += labelHeight + lineHeight;
            }
            contentHeight += padding;

            widgets.ScrollColumn(inner, contentHeight, ref debugScroll, content =>
            {
                float y = content.y + padding;
                float x = content.x + padding - 12f;
                float width = content.width - 2f * (padding - 12f);
                Color ToneColor(int tone) => tone switch
                {
                    1 => Palette.TextMuted,
                    2 => Palette.Accent,
                    3 => Palette.Success,
                    4 => Palette.Danger,
                    _ => Palette.Text,
                };
                foreach (NativeDebugRow r in rows)
                {
                    if (r.kind == 0)
                    {
                        y += 6f;
                        widgets.Label(new Rect(x, y, width, lineHeight), r.Label, TypeSizes.Body, Palette.Accent);
                        y += lineHeight + 4f;
                    }
                    else if (r.kind == 2)
                    {
                        int colorIndex = Mathf.Clamp(r.colorIndex, 0, Palette.Balls.Length - 1);
                        App.Theme.FillDisc(new Rect(x, y + 3f, 14f, 14f), Palette.Balls[colorIndex]);
                        widgets.Label(new Rect(x + 22f, y, width - 22f, lineHeight), r.Label, TypeSizes.Label,
                            Palette.TextMuted);
                        widgets.Label(new Rect(x, y, width, lineHeight), r.Value, TypeSizes.Label, Palette.Text,
                            TextAnchor.UpperRight);
                        y += lineHeight;
                    }
                    else
                    {
                        widgets.Label(new Rect(x, y, width, labelHeight), r.Label, TypeSizes.Small, Palette.TextMuted);
                        y += labelHeight;
                        widgets.Label(new Rect(x, y, width, lineHeight), r.Value, TypeSizes.Label, ToneColor(r.tone));
                        y += lineHeight;
                    }
                }
            });
        }

        private void DrawDebugGadgetBurst(BoardInfoData board, SessionInfo session, SessionStatsData stats)
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

                bool physicsBehind = session.state == BallsNative.StateRunning && stats.physicsFps < KeepingUpTickRate;
                widgets.Readout(new Rect(content.x + half + 12f, y, half, 40f), "Physics ms", $"{stats.physicsMs:F2}",
                    physicsBehind ? Palette.Danger : (Color?)null);
                y += 46f;
                widgets.Readout(new Rect(content.x, y, half, 40f), "GPU ms", GpuText());
                y += 46f;
                if (advancedOpen)
                {

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
                widgets.Readout(new Rect(content.x, y, content.width, 40f), "Waiting to spawn",
                    backend != null && backend.EndlessSpawn ? "Endless" : $"{stats.waitingToSpawn:N0}");
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

        private void SelectBallLimit(GameBackend backend)
        {
            if (backend == null)
            {
                return;
            }
            backend.SelectBallLimit(limitIndex);
        }

        private string BallLimitText(GameBackend backend)
        {
            if (backend.BallLimitIndex == 0)
            {
                return "Level";
            }
            if (backend.BallLimitIndex == 1)
            {
                return backend.HasMachineLimit ? $"Auto {backend.MachineLimit:N0}" : "Auto";
            }
            return backend.BallLimit == ulong.MaxValue ? "Max (2^64-1)" : $"{backend.BallLimit:N0}";
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
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), new Color(Palette.BoardBackdrop.r, Palette.BoardBackdrop.g, Palette.BoardBackdrop.b, 0.70f));
            float width = 420f;
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = theme.UnitsHeight * 0.32f;
            int buttonCount = App.FlowButtonCount;
            Rect panel = new Rect(x, y, width, 60f + buttonCount * (60f + 14f) + 24f);
            theme.Frame(panel, Palette.Panel, Palette.Border);

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "Paused";
            }
            widgets.Label(new Rect(panel.x, panel.y + 16f, panel.width, 44f), title, TypeSizes.Large,
                Palette.Text, TextAnchor.MiddleCenter, true);

            float buttonY = panel.y + 70f;
            for (int index = 0; index < buttonCount; ++index)
            {
                if (BallsNative.balls_flow_button(App.FlowHandle, index, out NativeFlowButton button) != 0)
                {
                    if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), button.Label, TypeSizes.Header, button.enabled != 0))
                    {
                        App.ActivateFlow(button.action);
                    }
                    buttonY += 74f;
                }
            }
        }

        private void DrawWinOverlay()
        {
            Widgets widgets = App.Widgets;
            UiTheme theme = App.Theme;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), new Color(Palette.BoardBackdrop.r, Palette.BoardBackdrop.g, Palette.BoardBackdrop.b, 0.70f));
            float width = 420f;
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = theme.UnitsHeight * 0.32f;
            int buttonCount = App.FlowButtonCount;
            Rect panel = new Rect(x, y, width, 60f + buttonCount * (60f + 14f) + 24f);
            theme.Frame(panel, Palette.Panel, Palette.Border);

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "Level Complete!";
            }
            widgets.Label(new Rect(panel.x, panel.y + 16f, panel.width, 44f), title, TypeSizes.Large,
                Palette.Accent, TextAnchor.MiddleCenter, true);

            float buttonY = panel.y + 70f;
            for (int index = 0; index < buttonCount; ++index)
            {
                if (BallsNative.balls_flow_button(App.FlowHandle, index, out NativeFlowButton button) != 0)
                {
                    if (widgets.Button(new Rect(panel.x + 24f, buttonY, panel.width - 48f, 60f), button.Label, TypeSizes.Header, button.enabled != 0))
                    {
                        App.ActivateFlow(button.action);
                    }
                    buttonY += 74f;
                }
            }
        }
    }
}
