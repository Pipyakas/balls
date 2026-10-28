using System;
using System.Collections.Generic;
using System.IO;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The map editor screen for the Unity front end.
    ///
    /// The C++ core owns the document state (paint, extend, erase, launcher rows, undo/redo, validation, save).
    /// This screen draws the brick grid, launcher grid, toolbars and property panels around it, matching the design
    /// and the SDL reference implementation.
    /// </summary>
    public sealed class EditorScreen : Screen
    {
        private enum EditorTool
        {
            Paint,
            Extend,
            Erase,
            Pick
        }

        private enum PortraitPanel
        {
            Tools,
            Level
        }

        private struct LauncherSlot
        {
            public bool present;
            public int color;
            public int count;
        }

        private struct LauncherSnapshot
        {
            public int columns;
            public int visibleRows;
            public LauncherSlot[] slots;
        }

        private struct Regions
        {
            public bool isPortrait;
            public Rect brickArea;
            public Rect launcherArea;
            public Rect tools;
            public Rect properties;
            public Rect panelTabs;
        }

        private struct GridCell
        {
            public int column;
            public int row;
            public bool isValid;

            public GridCell(int col, int r)
            {
                column = col;
                row = r;
                isValid = true;
            }
        }

        private const string DefaultLevelName = "NewLevel";
        private const int DefaultWidth = 10;
        private const int DefaultHeight = 8;
        private const int DefaultPlayableHeight = 20;
        private const int DefaultLauncherColumns = 3;
        private const int DefaultVisibleRows = 3;

        private IntPtr editor = IntPtr.Zero;
        private string levelName = DefaultLevelName;
        private string loadedLevelName = DefaultLevelName;
        private int targetMode = BallsApp.NormalMode;

        private EditorTool tool = EditorTool.Paint;
        private int brushColor = 0; // Red
        private int brushHits = 1;
        private int launcherBallCount = 10;

        private int selectedBrickId = -1;
        private int selectedCol = -1;
        private int selectedRow = -1;

        private int launcherColumns = DefaultLauncherColumns;
        private int launcherVisibleRows = DefaultVisibleRows;
        private List<LauncherSlot> launcherSlots = new();

        private readonly List<LauncherSnapshot> undoLauncherStack = new();
        private readonly List<LauncherSnapshot> redoLauncherStack = new();

        private bool confirmLeave;
        private bool strokeActive;
        private bool draggingBricks;
        private Vector2Int lastPaintedCell = new Vector2Int(-1, -1);

        private Vector2 toolsScroll;
        private Vector2 propertiesScroll;
        private float toolsContentHeight = 600f;
        private float propertiesContentHeight = 850f;
        private PortraitPanel portraitPanel = PortraitPanel.Tools;

        ~EditorScreen()
        {
            if (editor != IntPtr.Zero)
            {
                BallsNative.balls_editor_destroy(editor);
                editor = IntPtr.Zero;
            }
        }

        public override void Enter()
        {
            if (editor == IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
                StartNewLevel(targetMode);
            }
        }

        public override void Leave()
        {
            confirmLeave = false;
            strokeActive = false;
            draggingBricks = false;
            lastPaintedCell = new Vector2Int(-1, -1);
        }

        public override void HandleEscape()
        {
            if (GUI.GetNameOfFocusedControl() == "level_name")
            {
                GUI.FocusControl(null);
                return;
            }
            if (editor != IntPtr.Zero)
            {
                BallsNative.balls_editor_info(editor, out NativeEditorInfo info);
                if (info.dirty != 0 && !confirmLeave)
                {
                    confirmLeave = true;
                    return;
                }
            }
            confirmLeave = false;
            App.Go<MainMenuScreen>();
        }

        public void StartNewLevel(int forMode)
        {
            targetMode = forMode;
            levelName = DefaultLevelName;
            loadedLevelName = levelName;
            if (editor == IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
            }
            BallsNative.balls_editor_new_level(editor, levelName, DefaultWidth, DefaultHeight, DefaultPlayableHeight,
                DefaultLauncherColumns, DefaultVisibleRows);
            launcherColumns = DefaultLauncherColumns;
            launcherVisibleRows = DefaultVisibleRows;
            launcherSlots.Clear();
            undoLauncherStack.Clear();
            redoLauncherStack.Clear();
            confirmLeave = false;
            selectedBrickId = -1;
            selectedCol = -1;
            selectedRow = -1;
            App?.Widgets.ForgetTextField("level_name");
        }

        public void StartEditing(int forMode, string name)
        {
            targetMode = forMode;
            levelName = name;
            loadedLevelName = name;
            if (editor == IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
            }
            string directory = App.LevelDirectoryFor(forMode);
            int result = BallsNative.balls_editor_load_level(editor, directory, name);
            undoLauncherStack.Clear();
            redoLauncherStack.Clear();
            confirmLeave = false;
            selectedBrickId = -1;
            selectedCol = -1;
            selectedRow = -1;
            App?.Widgets.ForgetTextField("level_name");
            if (result == 0)
            {
                BallsNative.balls_editor_info(editor, out NativeEditorInfo info);
                launcherColumns = Mathf.Max(1, info.launcherColumns);
                launcherVisibleRows = Mathf.Max(1, info.launcherVisibleRows);
                LoadLauncherSlotsFromDisk(directory, name);
                App.Toast($"Loaded {name}");
            }
            else
            {
                App.Toast($"Cannot load {name}: {BallsNative.LastError()}", true);
            }
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            if (editor == IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
                StartNewLevel(targetMode);
            }

            BallsNative.balls_editor_info(editor, out NativeEditorInfo info);

            HandleShortcuts(info);

            Regions regions = ComputeRegions(theme.UnitsWidth, theme.UnitsHeight);

            HandleBrickInput(regions.brickArea, info.width, info.height, info.playableHeight);
            HandleLauncherInput(regions.launcherArea);

            DrawBrickGrid(regions.brickArea, info);
            DrawLauncherGrid(regions.launcherArea);

            if (!regions.isPortrait)
            {
                DrawToolPanel(regions.tools);
                DrawPropertyPanel(regions.properties, info);
            }
            else
            {
                DrawPanelTabs(regions.panelTabs);
                if (portraitPanel == PortraitPanel.Tools)
                {
                    DrawToolPanel(regions.tools);
                }
                else
                {
                    DrawPropertyPanel(regions.properties, info);
                }
            }
        }

        private Regions ComputeRegions(float width, float height)
        {
            const float margin = 16f;
            const float sidePanelWidth = 270f;
            const float propertyPanelWidth = 330f;
            const float brickAreaShare = 0.66f;
            const float minimumGridWidth = 320f;
            const float portraitGridShare = 0.55f;
            const float panelTabsHeight = 40f;
            const float controlGap = 8f;

            Regions regions = new Regions();
            float landscapeGridWidth = width - sidePanelWidth - propertyPanelWidth - margin * 4f;
            regions.isPortrait = width < height || landscapeGridWidth < minimumGridWidth;
            if (regions.isPortrait)
            {
                float contentWidth = width - margin * 2f;
                float gridsHeight = (height - margin * 4f - panelTabsHeight) * portraitGridShare;
                float brickHeight = (gridsHeight - margin) * brickAreaShare;
                regions.brickArea = new Rect(margin, margin, contentWidth, brickHeight);
                regions.launcherArea = new Rect(margin, margin * 2f + brickHeight, contentWidth, gridsHeight - margin - brickHeight);
                regions.panelTabs = new Rect(margin, margin * 2f + gridsHeight, contentWidth, panelTabsHeight);
                float panelTop = regions.panelTabs.y + panelTabsHeight + controlGap;
                regions.tools = new Rect(margin, panelTop, contentWidth, height - margin - panelTop);
                regions.properties = regions.tools;
                return regions;
            }

            regions.tools = new Rect(margin, margin, sidePanelWidth, height - margin * 2f);
            regions.properties = new Rect(width - propertyPanelWidth - margin, margin, propertyPanelWidth, height - margin * 2f);
            float centerX = regions.tools.x + regions.tools.width + margin;
            float centerWidth = regions.properties.x - margin - centerX;
            float brickHeightL = (height - margin * 3f) * brickAreaShare;
            regions.brickArea = new Rect(centerX, margin, centerWidth, brickHeightL);
            regions.launcherArea = new Rect(centerX, margin * 2f + brickHeightL, centerWidth, height - margin * 3f - brickHeightL);
            return regions;
        }

        private float BrickCellSize(Rect area, int width, int playableHeight)
        {
            Rect inner = new Rect(area.x + 14f, area.y + 14f, area.width - 28f, area.height - 28f);
            return Mathf.Max(2f, Mathf.Min(inner.width / Mathf.Max(width, 1), inner.height / Mathf.Max(playableHeight, 1)));
        }

        private Rect BrickGridRect(Rect area, int width, int playableHeight)
        {
            float cell = BrickCellSize(area, width, playableHeight);
            float gridWidth = cell * width;
            float gridHeight = cell * playableHeight;
            return new Rect(area.x + (area.width - gridWidth) * 0.5f, area.y + (area.height - gridHeight) * 0.5f, gridWidth, gridHeight);
        }

        private GridCell BrickCellAt(Rect area, Vector2 point, int width, int height, int playableHeight)
        {
            Rect grid = BrickGridRect(area, width, playableHeight);
            if (!grid.Contains(point))
            {
                return default;
            }
            float cell = BrickCellSize(area, width, playableHeight);
            int column = (int)((point.x - grid.x) / cell);
            int row = (int)((point.y - grid.y) / cell);
            if (column < 0 || column >= width || row < 0 || row >= height)
            {
                return default;
            }
            return new GridCell(column, row);
        }

        private float LauncherCellSize(Rect area, int columns, int rowCount)
        {
            Rect inner = new Rect(area.x + 14f, area.y + 14f, area.width - 28f, area.height - 28f);
            float rows = rowCount + 1f;
            float cols = Mathf.Max(columns, 1f);
            return Mathf.Max(14f, Mathf.Min(Mathf.Min(inner.width / cols, inner.height / rows), 64f));
        }

        private Rect LauncherGridRect(Rect area, int columns, int rowCount)
        {
            float cell = LauncherCellSize(area, columns, rowCount);
            float gridWidth = cell * columns;
            float gridHeight = cell * (rowCount + 1f);
            return new Rect(area.x + (area.width - gridWidth) * 0.5f, area.y + 14f, gridWidth, gridHeight);
        }

        private GridCell LauncherCellAt(Rect area, Vector2 point, int columns, int rowCount)
        {
            Rect grid = LauncherGridRect(area, columns, rowCount);
            if (!grid.Contains(point))
            {
                return default;
            }
            float cell = LauncherCellSize(area, columns, rowCount);
            int column = (int)((point.x - grid.x) / cell);
            int row = (int)((point.y - grid.y) / cell);
            if (column < 0 || column >= columns || row < 0 || row > rowCount)
            {
                return default;
            }
            return new GridCell(column, row);
        }

        private void SelectColor(int color)
        {
            brushColor = Mathf.Clamp(color, 0, 6);
            if (tool == EditorTool.Pick && selectedBrickId >= 0 && selectedCol >= 0 && selectedRow >= 0)
            {
                RecordUndoState();
                BallsNative.balls_editor_begin_edit(editor);
                BallsNative.balls_editor_set_brick_color(editor, selectedCol, selectedRow, brushColor);
                confirmLeave = false;
            }
        }

        private void ChangeSelectedHits(int hits)
        {
            if (tool == EditorTool.Pick && selectedBrickId >= 0 && selectedCol >= 0 && selectedRow >= 0)
            {
                RecordUndoState();
                BallsNative.balls_editor_begin_edit(editor);
                BallsNative.balls_editor_set_brick_hits(editor, selectedCol, selectedRow, hits);
                confirmLeave = false;
            }
        }

        private void RecordUndoState()
        {
            var snapshot = new LauncherSnapshot
            {
                columns = launcherColumns,
                visibleRows = launcherVisibleRows,
                slots = launcherSlots.ToArray()
            };
            undoLauncherStack.Add(snapshot);
            if (undoLauncherStack.Count > 100)
            {
                undoLauncherStack.RemoveAt(0);
            }
            redoLauncherStack.Clear();
        }

        private void Undo()
        {
            BallsNative.balls_editor_info(editor, out NativeEditorInfo info);
            if (info.canUndo != 0)
            {
                BallsNative.balls_editor_undo(editor);
                if (undoLauncherStack.Count > 0)
                {
                    var current = new LauncherSnapshot
                    {
                        columns = launcherColumns,
                        visibleRows = launcherVisibleRows,
                        slots = launcherSlots.ToArray()
                    };
                    redoLauncherStack.Add(current);
                    var prev = undoLauncherStack[undoLauncherStack.Count - 1];
                    undoLauncherStack.RemoveAt(undoLauncherStack.Count - 1);
                    launcherColumns = prev.columns;
                    launcherVisibleRows = prev.visibleRows;
                    launcherSlots = new List<LauncherSlot>(prev.slots);
                }
                confirmLeave = false;
                selectedBrickId = -1;
                selectedCol = -1;
                selectedRow = -1;
            }
        }

        private void Redo()
        {
            BallsNative.balls_editor_info(editor, out NativeEditorInfo info);
            if (info.canRedo != 0)
            {
                BallsNative.balls_editor_redo(editor);
                if (redoLauncherStack.Count > 0)
                {
                    var current = new LauncherSnapshot
                    {
                        columns = launcherColumns,
                        visibleRows = launcherVisibleRows,
                        slots = launcherSlots.ToArray()
                    };
                    undoLauncherStack.Add(current);
                    var next = redoLauncherStack[redoLauncherStack.Count - 1];
                    redoLauncherStack.RemoveAt(redoLauncherStack.Count - 1);
                    launcherColumns = next.columns;
                    launcherVisibleRows = next.visibleRows;
                    launcherSlots = new List<LauncherSlot>(next.slots);
                }
                confirmLeave = false;
                selectedBrickId = -1;
                selectedCol = -1;
                selectedRow = -1;
            }
        }

        private bool Save()
        {
            if (string.IsNullOrEmpty(levelName))
            {
                App.Toast("Enter a level name first", true);
                return false;
            }
            string directory = App.LevelDirectoryFor(targetMode);
            Directory.CreateDirectory(directory);

            int result = BallsNative.balls_editor_save_level(editor, directory);
            if (result != 0)
            {
                string err = BallsNative.LastError();
                App.Toast(string.IsNullOrEmpty(err) ? "Save failed" : err, true);
                return false;
            }

            if (!string.IsNullOrEmpty(loadedLevelName) && !loadedLevelName.Equals(levelName, StringComparison.Ordinal))
            {
                string oldBricks = Path.Combine(directory, $"{loadedLevelName}_Bricks.txt");
                string oldLaunchers = Path.Combine(directory, $"{loadedLevelName}_SpawnBall.txt");
                string newBricks = Path.Combine(directory, $"{levelName}_Bricks.txt");
                string newLaunchers = Path.Combine(directory, $"{levelName}_SpawnBall.txt");
                if (File.Exists(oldBricks))
                {
                    File.Copy(oldBricks, newBricks, true);
                    File.Delete(oldBricks);
                }
                if (File.Exists(oldLaunchers))
                {
                    File.Copy(oldLaunchers, newLaunchers, true);
                    File.Delete(oldLaunchers);
                }
                BallsNative.balls_editor_load_level(editor, directory, levelName);
                loadedLevelName = levelName;
            }
            else
            {
                loadedLevelName = levelName;
            }

            BallsNative.balls_editor_mark_saved(editor);
            confirmLeave = false;
            App.Toast($"Saved {levelName} ({(targetMode == BallsApp.NormalMode ? "normal" : "performance")})");
            return true;
        }

        private void HandleShortcuts(NativeEditorInfo info)
        {
            bool nameFocused = GUI.GetNameOfFocusedControl() == "level_name";

            if (nameFocused)
            {
                return;
            }

            bool ctrl = Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl) ||
                        Input.GetKey(KeyCode.LeftCommand) || Input.GetKey(KeyCode.RightCommand);

            if (ctrl)
            {
                if (Input.GetKeyDown(KeyCode.Z))
                {
                    Undo();
                }
                else if (Input.GetKeyDown(KeyCode.Y))
                {
                    Redo();
                }
                else if (Input.GetKeyDown(KeyCode.S))
                {
                    Save();
                }
                return;
            }

            if (Input.GetKeyDown(KeyCode.Alpha1)) SelectColor(0);
            else if (Input.GetKeyDown(KeyCode.Alpha2)) SelectColor(1);
            else if (Input.GetKeyDown(KeyCode.Alpha3)) SelectColor(2);
            else if (Input.GetKeyDown(KeyCode.Alpha4)) SelectColor(3);
            else if (Input.GetKeyDown(KeyCode.Alpha5)) SelectColor(4);
            else if (Input.GetKeyDown(KeyCode.Alpha6)) SelectColor(5);
            else if (Input.GetKeyDown(KeyCode.Alpha7)) SelectColor(6);
            else if (Input.GetKeyDown(KeyCode.P)) tool = EditorTool.Paint;
            else if (Input.GetKeyDown(KeyCode.E)) tool = EditorTool.Extend;
            else if (Input.GetKeyDown(KeyCode.X)) tool = EditorTool.Erase;
            else if (Input.GetKeyDown(KeyCode.K)) tool = EditorTool.Pick;
        }

        private void ApplyBrickTool(GridCell cell, bool erasing)
        {
            if (erasing || tool == EditorTool.Erase)
            {
                BallsNative.balls_editor_erase_cell(editor, cell.column, cell.row);
                return;
            }
            int brickUnderCursor = BallsNative.balls_editor_brick_at(editor, cell.column, cell.row);
            switch (tool)
            {
                case EditorTool.Paint:
                    BallsNative.balls_editor_paint_cell(editor, cell.column, cell.row, brushColor, brushHits);
                    break;
                case EditorTool.Extend:
                    if (!draggingBricks && brickUnderCursor >= 0)
                    {
                        selectedBrickId = brickUnderCursor;
                        selectedCol = cell.column;
                        selectedRow = cell.row;
                    }
                    else if (selectedBrickId >= 0)
                    {
                        BallsNative.balls_editor_extend_brick(editor, cell.column, cell.row);
                    }
                    break;
                case EditorTool.Pick:
                    selectedBrickId = brickUnderCursor;
                    if (selectedBrickId >= 0)
                    {
                        selectedCol = cell.column;
                        selectedRow = cell.row;
                        int col = BallsNative.balls_editor_brick_color(editor, cell.column, cell.row);
                        int hits = BallsNative.balls_editor_brick_hits(editor, cell.column, cell.row);
                        if (col >= 0) brushColor = col;
                        if (hits > 0) brushHits = hits;
                    }
                    break;
            }
        }

        private void HandleBrickInput(Rect area, int width, int height, int playableHeight)
        {
            if (!Input.GetMouseButton(0) && !Input.GetMouseButton(1))
            {
                draggingBricks = false;
                strokeActive = false;
                lastPaintedCell = new Vector2Int(-1, -1);
            }

            Vector2 mousePos = Event.current.mousePosition / App.Theme.Scale;
            bool isDown = Event.current.type == EventType.MouseDown;
            bool isDrag = Event.current.type == EventType.MouseDrag;
            bool isUp = Event.current.type == EventType.MouseUp || Event.current.rawType == EventType.MouseUp;

            if (isUp)
            {
                draggingBricks = false;
                strokeActive = false;
                lastPaintedCell = new Vector2Int(-1, -1);
                return;
            }

            if (!isDown && !isDrag)
            {
                return;
            }

            GridCell cell = BrickCellAt(area, mousePos, width, height, playableHeight);
            bool rightDown = Event.current.button == 1 || Input.GetMouseButton(1);

            if (isDown && cell.isValid)
            {
                RecordUndoState();
                BallsNative.balls_editor_begin_edit(editor);
                strokeActive = true;
                draggingBricks = false;
                lastPaintedCell = new Vector2Int(-1, -1);
                confirmLeave = false;
            }

            if (!strokeActive || !cell.isValid)
            {
                return;
            }

            if (cell.column == lastPaintedCell.x && cell.row == lastPaintedCell.y)
            {
                return;
            }

            ApplyBrickTool(cell, rightDown);
            draggingBricks = lastPaintedCell.x >= 0 || tool != EditorTool.Extend || selectedBrickId >= 0;
            lastPaintedCell = new Vector2Int(cell.column, cell.row);
            confirmLeave = false;
            Event.current.Use();
        }

        private void HandleLauncherInput(Rect area)
        {
            if (Event.current.type != EventType.MouseDown)
            {
                return;
            }
            Vector2 mousePos = Event.current.mousePosition / App.Theme.Scale;
            int rowCount = launcherColumns == 0 ? 0 : launcherSlots.Count / launcherColumns;
            GridCell cell = LauncherCellAt(area, mousePos, launcherColumns, rowCount);
            if (!cell.isValid)
            {
                return;
            }

            bool shift = Event.current.shift || Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift);
            bool right = Event.current.button == 1;
            bool left = Event.current.button == 0;

            if (right && shift)
            {
                if (cell.row < rowCount)
                {
                    RemoveLauncherRow(cell.row);
                }
                Event.current.Use();
                return;
            }

            if (right)
            {
                if (cell.row < rowCount)
                {
                    SetLauncher(cell.column, cell.row, false, 0, 0);
                }
                Event.current.Use();
                return;
            }

            if (left)
            {
                SetLauncher(cell.column, cell.row, true, brushColor, launcherBallCount);
                Event.current.Use();
            }
        }

        private void SetLauncherColumns(int columns)
        {
            columns = Mathf.Max(1, columns);
            if (columns == launcherColumns) return;
            RecordUndoState();
            BallsNative.balls_editor_begin_edit(editor);
            BallsNative.balls_editor_set_launcher_columns(editor, columns);
            int oldCols = launcherColumns;
            int rows = oldCols == 0 ? 0 : launcherSlots.Count / oldCols;
            var reshaped = new List<LauncherSlot>(rows * columns);
            for (int r = 0; r < rows; ++r)
            {
                for (int c = 0; c < columns; ++c)
                {
                    if (c < oldCols)
                    {
                        reshaped.Add(launcherSlots[r * oldCols + c]);
                    }
                    else
                    {
                        reshaped.Add(new LauncherSlot { present = false });
                    }
                }
            }
            launcherColumns = columns;
            launcherSlots = reshaped;
            confirmLeave = false;
        }

        private void SetVisibleRows(int rows)
        {
            rows = Mathf.Max(1, rows);
            if (rows == launcherVisibleRows) return;
            RecordUndoState();
            BallsNative.balls_editor_begin_edit(editor);
            BallsNative.balls_editor_set_visible_rows(editor, rows);
            launcherVisibleRows = rows;
            confirmLeave = false;
        }

        private void SetLauncher(int col, int row, bool present, int color, int count)
        {
            if (col < 0 || col >= launcherColumns || row < 0) return;
            RecordUndoState();
            BallsNative.balls_editor_begin_edit(editor);
            BallsNative.balls_editor_set_launcher(editor, col, row, present ? 1 : 0, color, count);
            int rowCount = launcherColumns == 0 ? 0 : launcherSlots.Count / launcherColumns;
            while (rowCount <= row)
            {
                for (int c = 0; c < launcherColumns; ++c)
                {
                    launcherSlots.Add(new LauncherSlot { present = false });
                }
                rowCount++;
            }
            launcherSlots[row * launcherColumns + col] = new LauncherSlot { present = present, color = color, count = count };
            confirmLeave = false;
        }

        private void RemoveLauncherRow(int row)
        {
            int rowCount = launcherColumns == 0 ? 0 : launcherSlots.Count / launcherColumns;
            if (row < 0 || row >= rowCount) return;
            RecordUndoState();
            BallsNative.balls_editor_begin_edit(editor);
            BallsNative.balls_editor_remove_launcher_row(editor, row);
            launcherSlots.RemoveRange(row * launcherColumns, launcherColumns);
            confirmLeave = false;
        }

        private void AutoFillLaunchers()
        {
            int ballsPerLauncher = Mathf.Max(1, launcherBallCount > 0 ? launcherBallCount : 10);
            RecordUndoState();
            BallsNative.balls_editor_begin_edit(editor);
            BallsNative.balls_editor_auto_fill_launchers(editor, ballsPerLauncher);

            BallsNative.balls_editor_validate(editor, targetMode, out NativeValidation val);
            ulong[] brickHits = { val.brickHits0, val.brickHits1, val.brickHits2, val.brickHits3, val.brickHits4, val.brickHits5, val.brickHits6 };
            var chunks = new List<(int color, int count)>();
            for (int c = 0; c < 7; ++c)
            {
                int remaining = (int)brickHits[c];
                while (remaining > 0)
                {
                    int take = Mathf.Min(remaining, ballsPerLauncher);
                    chunks.Add((c, take));
                    remaining -= take;
                }
            }
            int cols = Mathf.Max(1, launcherColumns);
            int rows = chunks.Count == 0 ? 0 : (chunks.Count + cols - 1) / cols;
            launcherSlots = new List<LauncherSlot>(rows * cols);
            for (int i = 0; i < rows * cols; ++i)
            {
                if (i < chunks.Count)
                {
                    launcherSlots.Add(new LauncherSlot { present = true, color = chunks[i].color, count = chunks[i].count });
                }
                else
                {
                    launcherSlots.Add(new LauncherSlot { present = false });
                }
            }
            confirmLeave = false;
        }

        private void LoadLauncherSlotsFromDisk(string directory, string name)
        {
            launcherSlots.Clear();
            string path = Path.Combine(directory, $"{name}_SpawnBall.txt");
            if (!File.Exists(path))
            {
                return;
            }
            try
            {
                string[] lines = File.ReadAllLines(path);
                bool inBoard = false;
                bool inLayout = false;
                foreach (string rawLine in lines)
                {
                    string line = rawLine.Trim();
                    if (string.IsNullOrEmpty(line)) continue;
                    if (line.StartsWith("[") && line.EndsWith("]"))
                    {
                        string section = line.Substring(1, line.Length - 2).Trim();
                        inBoard = section.Equals("Board", StringComparison.OrdinalIgnoreCase);
                        inLayout = section.Equals("Layout", StringComparison.OrdinalIgnoreCase);
                        continue;
                    }
                    if (inBoard)
                    {
                        string[] parts = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length == 2)
                        {
                            if (parts[0].Equals("Width", StringComparison.OrdinalIgnoreCase) && int.TryParse(parts[1], out int w))
                            {
                                launcherColumns = Mathf.Max(1, w);
                            }
                            else if (parts[0].Equals("Height", StringComparison.OrdinalIgnoreCase) && int.TryParse(parts[1], out int h))
                            {
                                launcherVisibleRows = Mathf.Max(1, h);
                            }
                        }
                    }
                    else if (inLayout)
                    {
                        string[] tokens = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                        foreach (string token in tokens)
                        {
                            if (token.Equals("Empty", StringComparison.OrdinalIgnoreCase) || token == ".")
                            {
                                launcherSlots.Add(new LauncherSlot { present = false });
                            }
                            else
                            {
                                int open = token.IndexOf('(');
                                int close = token.IndexOf(')');
                                if (open > 0 && close > open)
                                {
                                    string colStr = token.Substring(0, open);
                                    string cntStr = token.Substring(open + 1, close - open - 1);
                                    int c = ParseBallColor(colStr);
                                    int.TryParse(cntStr, out int cnt);
                                    launcherSlots.Add(new LauncherSlot { present = true, color = c, count = Mathf.Max(1, cnt) });
                                }
                                else
                                {
                                    launcherSlots.Add(new LauncherSlot { present = false });
                                }
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"Failed to parse {path}: {ex.Message}");
            }
        }

        private static int ParseBallColor(string name)
        {
            switch (name.ToLowerInvariant())
            {
                case "red": return 0;
                case "orange": return 1;
                case "yellow": return 2;
                case "green": return 3;
                case "blue": return 4;
                case "indigo": return 5;
                case "violet": return 6;
                default: return 0;
            }
        }

        private void DrawBrickGrid(Rect area, NativeEditorInfo info)
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Frame(area, Palette.BoardBackdrop, Palette.Border);

            int width = info.width;
            int height = info.height;
            int playableHeight = info.playableHeight;
            float cell = BrickCellSize(area, width, playableHeight);
            Rect grid = BrickGridRect(area, width, playableHeight);

            theme.Fill(grid, Palette.Background);
            theme.Fill(new Rect(grid.x, grid.y, grid.width, cell * height), Palette.Panel);

            Vector2 mousePos = Event.current.mousePosition / theme.Scale;
            GridCell hovered = BrickCellAt(area, mousePos, width, height, playableHeight);

            int[] cellIds = new int[width * height];
            for (int r = 0; r < height; ++r)
            {
                for (int c = 0; c < width; ++c)
                {
                    cellIds[r * width + c] = BallsNative.balls_editor_brick_at(editor, c, r);
                }
            }

            for (int row = 0; row < height; ++row)
            {
                for (int col = 0; col < width; ++col)
                {
                    int id = cellIds[row * width + col];
                    Rect cellRect = new Rect(grid.x + col * cell, grid.y + row * cell, cell, cell);

                    if (id >= 0)
                    {
                        int color = BallsNative.balls_editor_brick_color(editor, col, row);
                        int hits = BallsNative.balls_editor_brick_hits(editor, col, row);
                        Color fill = Palette.Brick(color);
                        if (id == selectedBrickId)
                        {
                            fill = UiTheme.Lighten(fill, 0.35f);
                        }
                        theme.Fill(cellRect, fill);

                        float border = Mathf.Max(1f, cell * 0.08f);
                        Color edge = new Color(fill.r * 0.45f, fill.g * 0.45f, fill.b * 0.45f, 1f);

                        if (col == 0 || cellIds[row * width + (col - 1)] != id)
                        {
                            theme.Fill(new Rect(cellRect.x, cellRect.y, border, cellRect.height), edge);
                        }
                        if (col == width - 1 || cellIds[row * width + (col + 1)] != id)
                        {
                            theme.Fill(new Rect(cellRect.xMax - border, cellRect.y, border, cellRect.height), edge);
                        }
                        if (row == 0 || cellIds[(row - 1) * width + col] != id)
                        {
                            theme.Fill(new Rect(cellRect.x, cellRect.y, cellRect.width, border), edge);
                        }
                        if (row == height - 1 || cellIds[(row + 1) * width + col] != id)
                        {
                            theme.Fill(new Rect(cellRect.x, cellRect.yMax - border, cellRect.width, border), edge);
                        }

                        bool firstCellOfBrick = (col == 0 || cellIds[row * width + (col - 1)] != id) &&
                                                (row == 0 || cellIds[(row - 1) * width + col] != id);
                        if (hits > 1 && firstCellOfBrick && cell >= 12f)
                        {
                            int labelSize = Mathf.Max(8, Mathf.RoundToInt(cell * 0.55f));
                            GUI.Label(theme.Units(cellRect.x, cellRect.y, cellRect.width, cellRect.height),
                                hits.ToString(), theme.TextStyle(labelSize, Palette.TextStrong, true, TextAnchor.MiddleCenter));
                        }
                    }
                    else
                    {
                        theme.Frame(cellRect, Palette.Panel, Palette.Border);
                    }

                    if (hovered.isValid && hovered.column == col && hovered.row == row)
                    {
                        theme.Frame(cellRect, UiTheme.Clear, Palette.Accent);
                    }
                }
            }

            string size = $"{width} x {height}";
            string footer = $"{size}  |  L-drag: tool  R-drag: erase  Ctrl+Z/Y";
            widgets.Muted(new Rect(area.x + 10f, area.yMax - 26f, area.width - 20f, 20f), footer, TypeSizes.Small);
        }

        private void DrawLauncherGrid(Rect area)
        {
            UiTheme theme = App.Theme;
            theme.Frame(area, Palette.Panel, Palette.Border);

            int rowCount = launcherColumns == 0 ? 0 : launcherSlots.Count / launcherColumns;
            float cell = LauncherCellSize(area, launcherColumns, rowCount);
            float gridWidth = cell * launcherColumns;
            float gridHeight = cell * (rowCount + 1f);
            Rect grid = new Rect(area.x + (area.width - gridWidth) * 0.5f, area.y + 14f, gridWidth, gridHeight);

            Vector2 mousePos = Event.current.mousePosition / theme.Scale;
            GridCell hovered = LauncherCellAt(area, mousePos, launcherColumns, rowCount);

            for (int row = 0; row <= rowCount; ++row)
            {
                for (int col = 0; col < launcherColumns; ++col)
                {
                    Rect rect = new Rect(grid.x + col * cell + 3f, grid.y + row * cell + 3f, cell - 6f, cell - 6f);
                    bool exists = row < rowCount;
                    LauncherSlot slot = exists ? launcherSlots[row * launcherColumns + col] : default;

                    if (exists && slot.present)
                    {
                        theme.Fill(rect, Palette.Balls[slot.color]);
                        int textSize = Mathf.Max(10, Mathf.RoundToInt(cell * 0.36f));
                        GUI.Label(theme.Units(rect.x, rect.y, rect.width, rect.height), slot.count.ToString(),
                            theme.TextStyle(textSize, Palette.TextStrong, true, TextAnchor.MiddleCenter));
                    }
                    else
                    {
                        theme.Frame(rect, Palette.Panel, exists ? Palette.TextDim : Palette.Border);
                    }

                    if (row == launcherVisibleRows - 1)
                    {
                        theme.Fill(new Rect(grid.x, rect.yMax + 4f, gridWidth, 2f), UiTheme.WithAlpha(Palette.Accent, 0.6f));
                    }

                    if (hovered.isValid && hovered.column == col && hovered.row == row)
                    {
                        theme.Frame(rect, UiTheme.Clear, Palette.Accent);
                    }
                }
            }

            BallsNative.balls_editor_validate(editor, targetMode, out NativeValidation validation);
            ulong[] hits = { validation.brickHits0, validation.brickHits1, validation.brickHits2, validation.brickHits3, validation.brickHits4, validation.brickHits5, validation.brickHits6 };
            ulong[] balls = { validation.balls0, validation.balls1, validation.balls2, validation.balls3, validation.balls4, validation.balls5, validation.balls6 };
            float x = area.x + 12f;
            float y = area.yMax - 30f;
            for (int c = 0; c < 7; ++c)
            {
                if (hits[c] == 0 && balls[c] == 0) continue;
                if (x > area.xMax - 50f) break;
                theme.FillDisc(new Rect(x, y + 2f, 14f, 14f), Palette.Balls[c]);
                string text = $"{balls[c]}/{hits[c]}";
                Color color = balls[c] == hits[c] ? Palette.Success : Palette.Danger;
                GUI.Label(theme.Units(x + 18f, y, 120f, 20f), text, theme.TextStyle(TypeSizes.Value, color, true, TextAnchor.MiddleLeft));
                x += 20f + text.Length * 11f + 14f;
            }
        }

        private void DrawToolPanel(Rect area)
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Frame(area, Palette.Panel, Palette.Border);

            Rect inner = new Rect(area.x + 12f, area.y + 12f, area.width - 24f, area.height - 24f);

            widgets.ScrollColumn(inner, toolsContentHeight, ref toolsScroll, content =>
            {
                float cursor = content.y + 8f;

                SectionTitle(content, ref cursor, "Tool");
                string[] toolNames = { "Paint (P)", "Extend (E)", "Erase (X)", "Pick (K)" };
                EditorTool[] toolKinds = { EditorTool.Paint, EditorTool.Extend, EditorTool.Erase, EditorTool.Pick };
                for (int i = 0; i < toolNames.Length; ++i)
                {
                    if (widgets.AccentButton(NextControl(content, ref cursor), toolNames[i], tool == toolKinds[i], TypeSizes.Header))
                    {
                        tool = toolKinds[i];
                    }
                }

                SectionTitle(content, ref cursor, "Color (1-7)");
                Rect swatchRow1 = NextControl(content, ref cursor, 34f);
                Rect swatchRow2 = NextControl(content, ref cursor, 34f);
                for (int i = 0; i < 7; ++i)
                {
                    Rect row = i < 4 ? swatchRow1 : swatchRow2;
                    Rect swatch = ColumnSlice(row, i % 4, 4, 8f);
                    theme.Fill(swatch, Palette.Balls[i]);
                    if (i == brushColor)
                    {
                        theme.Frame(new Rect(swatch.x - 2f, swatch.y - 2f, swatch.width + 4f, swatch.height + 4f), UiTheme.Clear, Palette.TextStrong);
                    }
                    if (GUI.Button(theme.Units(swatch.x, swatch.y, swatch.width, swatch.height), GUIContent.none, GUIStyle.none))
                    {
                        SelectColor(i);
                    }
                }

                SectionTitle(content, ref cursor, "Brick");
                if (widgets.Stepper(NextControl(content, ref cursor), "Hits", ref brushHits, 1, 999, out _))
                {
                    ChangeSelectedHits(brushHits);
                }
                string selection = selectedBrickId >= 0 ? $"Selected id {selectedBrickId}" : "No brick selected";
                widgets.Muted(NextControl(content, ref cursor, 24f), selection, TypeSizes.Value);

                SectionTitle(content, ref cursor, "Launcher");
                widgets.Stepper(NextControl(content, ref cursor), "Balls", ref launcherBallCount, 1, 100000, out _);
                widgets.Muted(NextControl(content, ref cursor, 22f), "L-click set, R-click clear", TypeSizes.Small);
                widgets.Muted(NextControl(content, ref cursor, 22f), "Shift+R-click delete row", TypeSizes.Small);

                toolsContentHeight = Mathf.Max(inner.height, cursor - content.y + 16f);
            });
        }

        private void DrawPropertyPanel(Rect area, NativeEditorInfo info)
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Frame(area, Palette.Panel, Palette.Border);

            Rect inner = new Rect(area.x + 12f, area.y + 12f, area.width - 24f, area.height - 24f);

            widgets.ScrollColumn(inner, propertiesContentHeight, ref propertiesScroll, content =>
            {
                float cursor = content.y + 8f;

                SectionTitle(content, ref cursor, "Level");
                widgets.TextField("level_name", NextControl(content, ref cursor), ref levelName, 40);

                Rect modeRow = NextControl(content, ref cursor);
                if (widgets.AccentButton(ColumnSlice(modeRow, 0, 2, 8f), "Normal", targetMode == BallsApp.NormalMode, TypeSizes.Header))
                {
                    targetMode = BallsApp.NormalMode;
                }
                if (widgets.AccentButton(ColumnSlice(modeRow, 1, 2, 8f), "Perf", targetMode == BallsApp.PerformanceMode, TypeSizes.Header))
                {
                    targetMode = BallsApp.PerformanceMode;
                }

                SectionTitle(content, ref cursor, "Board");
                int width = info.width;
                int height = info.height;
                int playable = info.playableHeight;
                bool wChanged = widgets.Stepper(NextControl(content, ref cursor), "Width", ref width, 1, 200, out _);
                bool hChanged = widgets.Stepper(NextControl(content, ref cursor), "Brick rows", ref height, 1, 200, out _);
                bool pChanged = widgets.Stepper(NextControl(content, ref cursor), "Total rows", ref playable, height, 400, out _);
                if (wChanged || hChanged || pChanged)
                {
                    RecordUndoState();
                    BallsNative.balls_editor_begin_edit(editor);
                    BallsNative.balls_editor_resize_board(editor, width, height, Mathf.Max(playable, height));
                    confirmLeave = false;
                }

                SectionTitle(content, ref cursor, "Launchers");
                int cols = launcherColumns;
                int vRows = launcherVisibleRows;
                if (widgets.Stepper(NextControl(content, ref cursor), "Columns", ref cols, 1, 8, out _))
                {
                    SetLauncherColumns(cols);
                }
                if (widgets.Stepper(NextControl(content, ref cursor), "Visible rows", ref vRows, 1, 8, out _))
                {
                    SetVisibleRows(vRows);
                }
                if (widgets.Button(NextControl(content, ref cursor), "Auto-fill to match bricks", TypeSizes.Header))
                {
                    AutoFillLaunchers();
                }

                SectionTitle(content, ref cursor, "Validation");
                BallsNative.balls_editor_validate(editor, targetMode, out NativeValidation validation);
                if (validation.playable != 0)
                {
                    widgets.Label(NextControl(content, ref cursor, 26f), "Ready to play", TypeSizes.Header, Palette.Success, TextAnchor.MiddleLeft, true);
                }
                else
                {
                    var buf = new byte[256];
                    for (int p = 0; p < validation.problemCount; ++p)
                    {
                        int len = BallsNative.balls_editor_problem(editor, p, buf, buf.Length);
                        string prob = len > 0 ? System.Text.Encoding.UTF8.GetString(buf, 0, len) : "";
                        widgets.Label(NextControl(content, ref cursor, 22f), prob, TypeSizes.Small, Palette.Danger);
                    }
                }

                float buttonsTop = content.y + inner.height - (38f + 8f) * 4f - 8f;
                cursor = Mathf.Max(cursor + 8f, buttonsTop);

                Rect undoRow = NextControl(content, ref cursor);
                if (widgets.Button(ColumnSlice(undoRow, 0, 2, 8f), "Undo", TypeSizes.Header, info.canUndo != 0))
                {
                    Undo();
                }
                if (widgets.Button(ColumnSlice(undoRow, 1, 2, 8f), "Redo", TypeSizes.Header, info.canRedo != 0))
                {
                    Redo();
                }

                if (widgets.Button(NextControl(content, ref cursor), info.dirty != 0 ? "Save *  (Ctrl+S)" : "Save  (Ctrl+S)", TypeSizes.Header))
                {
                    Save();
                }

                if (widgets.Button(NextControl(content, ref cursor), "Save & Play", TypeSizes.Header, validation.playable != 0))
                {
                    if (Save())
                    {
                        App.StartGameplay(targetMode, levelName, App.BackendKind);
                    }
                }

                if (widgets.Button(NextControl(content, ref cursor), confirmLeave ? "Discard changes?" : "Back", TypeSizes.Header))
                {
                    if (info.dirty != 0 && !confirmLeave)
                    {
                        confirmLeave = true;
                    }
                    else
                    {
                        confirmLeave = false;
                        App.Go<MainMenuScreen>();
                    }
                }

                propertiesContentHeight = Mathf.Max(inner.height, cursor - content.y + 16f);
            });
        }

        private void DrawPanelTabs(Rect area)
        {
            Widgets widgets = App.Widgets;
            if (widgets.AccentButton(ColumnSlice(area, 0, 2, 8f), "Tools", portraitPanel == PortraitPanel.Tools, TypeSizes.Header))
            {
                portraitPanel = PortraitPanel.Tools;
            }
            if (widgets.AccentButton(ColumnSlice(area, 1, 2, 8f), "Level", portraitPanel == PortraitPanel.Level, TypeSizes.Header))
            {
                portraitPanel = PortraitPanel.Level;
            }
        }

        private void SectionTitle(Rect content, ref float cursor, string text)
        {
            cursor += 6f;
            App.Widgets.Label(new Rect(content.x, cursor, content.width, 24f), text, TypeSizes.Header, Palette.Accent, TextAnchor.MiddleLeft, true);
            cursor += 30f;
        }

        private static Rect NextControl(Rect content, ref float cursor, float height = 38f)
        {
            Rect rect = new Rect(content.x, cursor, content.width, height);
            cursor += height + 8f;
            return rect;
        }

        private static Rect ColumnSlice(Rect row, int index, int count, float gap)
        {
            float totalGap = gap * (count - 1);
            float width = (row.width - totalGap) / count;
            return new Rect(row.x + index * (width + gap), row.y, width, row.height);
        }
    }
}
