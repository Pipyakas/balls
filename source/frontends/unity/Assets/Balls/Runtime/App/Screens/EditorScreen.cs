using System;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{

    public sealed class EditorScreen : Screen
    {
        private enum PortraitPanel
        {
            Tools,
            Level
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

        private IntPtr session = IntPtr.Zero;
        private int targetMode = BallsApp.NormalMode;

        private bool strokeActive;

        private Vector2 toolsScroll;
        private Vector2 propertiesScroll;
        private float toolsContentHeight = 600f;
        private float propertiesContentHeight = 850f;
        private PortraitPanel portraitPanel = PortraitPanel.Tools;

        ~EditorScreen()
        {
            if (session != IntPtr.Zero)
            {
                BallsNative.balls_editor_session_destroy(session);
                session = IntPtr.Zero;
            }
        }

        public override void Enter()
        {
            if (session == IntPtr.Zero)
            {
                StartNewLevel(targetMode);
            }
        }

        public override void Leave()
        {
            strokeActive = false;
        }

        public override void HandleEscape()
        {
            if (GUI.GetNameOfFocusedControl() == "level_name")
            {
                GUI.FocusControl(null);
                return;
            }
            if (session != IntPtr.Zero)
            {
                if (BallsNative.balls_editor_session_back(session) != 0)
                {
                    App.ActivateFlow(BallsNative.FlowBack);
                }
            }
            else
            {
                App.ActivateFlow(BallsNative.FlowBack);
            }
        }

        public void StartNewLevel(int forMode)
        {
            targetMode = forMode;
            if (session != IntPtr.Zero)
            {
                BallsNative.balls_editor_session_destroy(session);
            }
            string normal = App.LevelDirectoryFor(BallsApp.NormalMode);
            string perf = App.LevelDirectoryFor(BallsApp.PerformanceMode);
            session = BallsNative.balls_editor_session_create(forMode, null, normal, perf);
            App?.Widgets.ForgetTextField("level_name");
        }

        public void StartEditing(int forMode, string name)
        {
            targetMode = forMode;
            if (session != IntPtr.Zero)
            {
                BallsNative.balls_editor_session_destroy(session);
            }
            string normal = App.LevelDirectoryFor(BallsApp.NormalMode);
            string perf = App.LevelDirectoryFor(BallsApp.PerformanceMode);
            session = BallsNative.balls_editor_session_create(forMode, name, normal, perf);
            App?.Widgets.ForgetTextField("level_name");
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            if (session == IntPtr.Zero)
            {
                StartNewLevel(targetMode);
            }

            BallsNative.balls_editor_session_update(session, Time.deltaTime);
            BallsNative.balls_editor_session_state(session, out NativeEditorState state);

            HandleShortcuts();

            Regions regions = ComputeRegions(theme.UnitsWidth, theme.UnitsHeight);

            HandleBrickInput(regions.brickArea, state.width, state.height, state.playableHeight);
            HandleLauncherInput(regions.launcherArea, state.launcherColumns, state.launcherRows);

            DrawBrickGrid(regions.brickArea, state);
            DrawLauncherGrid(regions.launcherArea, state);

            if (!regions.isPortrait)
            {
                DrawToolPanel(regions.tools, state);
                DrawPropertyPanel(regions.properties, state);
            }
            else
            {
                DrawPanelTabs(regions.panelTabs);
                if (portraitPanel == PortraitPanel.Tools)
                {
                    DrawToolPanel(regions.tools, state);
                }
                else
                {
                    DrawPropertyPanel(regions.properties, state);
                }
            }

            if (state.messageSecondsLeft > 0f)
            {
                string msg = BallsNative.EditorText(session, BallsNative.EditorTextMessage);
                if (!string.IsNullOrEmpty(msg))
                {
                    App.Widgets.Toast(new Rect(0f, 24f, theme.UnitsWidth, 44f), msg, state.messageIsError != 0);
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

        private void HandleShortcuts()
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
                    BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutUndo);
                }
                else if (Input.GetKeyDown(KeyCode.Y))
                {
                    BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutRedo);
                }
                else if (Input.GetKeyDown(KeyCode.S))
                {
                    BallsNative.balls_editor_session_save(session);
                }
                return;
            }

            if (Input.GetKeyDown(KeyCode.Alpha1)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 0);
            else if (Input.GetKeyDown(KeyCode.Alpha2)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 1);
            else if (Input.GetKeyDown(KeyCode.Alpha3)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 2);
            else if (Input.GetKeyDown(KeyCode.Alpha4)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 3);
            else if (Input.GetKeyDown(KeyCode.Alpha5)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 4);
            else if (Input.GetKeyDown(KeyCode.Alpha6)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 5);
            else if (Input.GetKeyDown(KeyCode.Alpha7)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutColor1 + 6);
            else if (Input.GetKeyDown(KeyCode.P)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutPaint);
            else if (Input.GetKeyDown(KeyCode.E)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutExtend);
            else if (Input.GetKeyDown(KeyCode.X)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutErase);
            else if (Input.GetKeyDown(KeyCode.K)) BallsNative.balls_editor_session_shortcut(session, BallsNative.ShortcutPick);
        }

        private void HandleBrickInput(Rect area, int width, int height, int playableHeight)
        {
            if (!Input.GetMouseButton(0) && !Input.GetMouseButton(1))
            {
                if (strokeActive)
                {
                    BallsNative.balls_editor_session_end_stroke(session);
                    strokeActive = false;
                }
            }

            Vector2 mousePos = Event.current.mousePosition / App.Theme.Scale;
            bool isDown = Event.current.type == EventType.MouseDown;
            bool isDrag = Event.current.type == EventType.MouseDrag;
            bool isUp = Event.current.type == EventType.MouseUp || Event.current.rawType == EventType.MouseUp;

            if (isUp)
            {
                if (strokeActive)
                {
                    BallsNative.balls_editor_session_end_stroke(session);
                    strokeActive = false;
                }
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
                BallsNative.balls_editor_session_begin_stroke(session, rightDown ? 1 : 0);
                BallsNative.balls_editor_session_stroke_cell(session, cell.column, cell.row);
                strokeActive = true;
                Event.current.Use();
            }
            else if (strokeActive && cell.isValid)
            {
                BallsNative.balls_editor_session_stroke_cell(session, cell.column, cell.row);
                Event.current.Use();
            }
        }

        private void HandleLauncherInput(Rect area, int launcherColumns, int launcherRows)
        {
            if (Event.current.type != EventType.MouseDown)
            {
                return;
            }
            Vector2 mousePos = Event.current.mousePosition / App.Theme.Scale;
            GridCell cell = LauncherCellAt(area, mousePos, launcherColumns, launcherRows);
            if (!cell.isValid)
            {
                return;
            }

            bool shift = Event.current.shift || Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift);
            bool right = Event.current.button == 1;
            bool left = Event.current.button == 0;

            if (right && shift)
            {
                BallsNative.balls_editor_session_click_launcher(session, cell.column, cell.row, BallsNative.LauncherDeleteRow);
                Event.current.Use();
                return;
            }

            if (right)
            {
                BallsNative.balls_editor_session_click_launcher(session, cell.column, cell.row, BallsNative.LauncherClear);
                Event.current.Use();
                return;
            }

            if (left)
            {
                BallsNative.balls_editor_session_click_launcher(session, cell.column, cell.row, BallsNative.LauncherSet);
                Event.current.Use();
            }
        }

        private void DrawBrickGrid(Rect area, NativeEditorState state)
        {
            UiTheme theme = App.Theme;
            theme.Frame(area, Palette.BoardBackdrop, Palette.Border);

            int width = state.width;
            int height = state.height;
            int playableHeight = state.playableHeight;
            float cell = BrickCellSize(area, width, playableHeight);
            Rect grid = BrickGridRect(area, width, playableHeight);

            theme.Fill(grid, Palette.Background);
            theme.Fill(new Rect(grid.x, grid.y, grid.width, cell * height), Palette.Panel);

            Vector2 mousePos = Event.current.mousePosition / theme.Scale;
            GridCell hovered = BrickCellAt(area, mousePos, width, height, playableHeight);

            int[] cellIds = new int[width * height];
            int[] cellColors = new int[width * height];
            int[] cellHits = new int[width * height];
            for (int r = 0; r < height; ++r)
            {
                for (int c = 0; c < width; ++c)
                {
                    int index = r * width + c;
                    cellIds[index] = BallsNative.balls_editor_session_brick(session, c, r, out cellColors[index], out cellHits[index]);
                }
            }

            for (int row = 0; row < height; ++row)
            {
                for (int column = 0; column < width; ++column)
                {
                    int id = cellIds[row * width + column];
                    Rect rect = new Rect(grid.x + column * cell, grid.y + row * cell, cell, cell);
                    if (id >= 0)
                    {
                        int color = cellColors[row * width + column];
                        int hits = cellHits[row * width + column];
                        Color fill = Palette.Brick(color);
                        if (id == state.selectedBrick)
                        {
                            fill = UiTheme.Lighten(fill, 0.35f);
                        }
                        theme.Fill(rect, fill);

                        float border = Mathf.Max(1f, cell * 0.08f);
                        Color edge = new Color(fill.r * 0.45f, fill.g * 0.45f, fill.b * 0.45f, 1f);
                        if (column == 0 || cellIds[row * width + column - 1] != id)
                        {
                            theme.Fill(new Rect(rect.x, rect.y, border, rect.height), edge);
                        }
                        if (column == width - 1 || cellIds[row * width + column + 1] != id)
                        {
                            theme.Fill(new Rect(rect.xMax - border, rect.y, border, rect.height), edge);
                        }
                        if (row == 0 || cellIds[(row - 1) * width + column] != id)
                        {
                            theme.Fill(new Rect(rect.x, rect.y, rect.width, border), edge);
                        }
                        if (row == height - 1 || cellIds[(row + 1) * width + column] != id)
                        {
                            theme.Fill(new Rect(rect.x, rect.yMax - border, rect.width, border), edge);
                        }

                        bool firstCellOfBrick = (column == 0 || cellIds[row * width + column - 1] != id) &&
                                                (row == 0 || cellIds[(row - 1) * width + column] != id);
                        if (hits > 1 && firstCellOfBrick && cell >= 12f)
                        {
                            int labelSize = Mathf.Max(8, Mathf.RoundToInt(cell * 0.55f));
                            GUI.Label(theme.Units(rect.x, rect.y, rect.width, rect.height), hits.ToString(),
                                theme.TextStyle(labelSize, Palette.TextStrong, true, TextAnchor.MiddleCenter));
                        }
                    }
                    else
                    {
                        theme.Frame(rect, Palette.Panel, Palette.Border);
                    }

                    if (hovered.isValid && hovered.column == column && hovered.row == row)
                    {
                        theme.Frame(rect, UiTheme.Clear, Palette.Accent);
                    }
                }
            }

            string footer = BallsNative.EditorText(session, BallsNative.EditorTextFooter);
            if (string.IsNullOrEmpty(footer))
            {
                footer = $"{width} x {height}";
            }
            App.Widgets.Label(new Rect(area.x + 10f, area.y + area.height - 26f, area.width - 20f, 22f), footer, TypeSizes.Small, Palette.TextMuted);
        }

        private void DrawLauncherGrid(Rect area, NativeEditorState state)
        {
            UiTheme theme = App.Theme;
            theme.Frame(area, Palette.Panel, Palette.Border);

            int columns = state.launcherColumns;
            int rowCount = state.launcherRows;
            Rect grid = LauncherGridRect(area, columns, rowCount);
            float cell = LauncherCellSize(area, columns, rowCount);

            Vector2 mousePos = Event.current.mousePosition / theme.Scale;
            GridCell hovered = LauncherCellAt(area, mousePos, columns, rowCount);

            for (int row = 0; row <= rowCount; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    Rect rect = new Rect(grid.x + col * cell + 3f, grid.y + row * cell + 3f, cell - 6f, cell - 6f);
                    int present = BallsNative.balls_editor_session_launcher(session, col, row, out int color, out int balls);
                    if (present != 0)
                    {
                        theme.Fill(rect, Palette.Balls[Mathf.Clamp(color, 0, 6)]);
                        int textSize = Mathf.Max(10, Mathf.RoundToInt(cell * 0.36f));
                        GUI.Label(theme.Units(rect.x, rect.y, rect.width, rect.height), balls.ToString(),
                            theme.TextStyle(textSize, Palette.TextStrong, true, TextAnchor.MiddleCenter));
                    }
                    else
                    {
                        bool exists = row < rowCount;
                        theme.Frame(rect, Palette.Panel, exists ? Palette.TextDim : Palette.Border);
                    }

                    if (row == state.launcherVisibleRows - 1)
                    {
                        theme.Fill(new Rect(grid.x, rect.yMax + 4f, grid.width, 2f), UiTheme.WithAlpha(Palette.Accent, 0.6f));
                    }

                    if (hovered.isValid && hovered.column == col && hovered.row == row)
                    {
                        theme.Frame(rect, UiTheme.Clear, Palette.Accent);
                    }
                }
            }

            float x = area.x + 12f;
            float y = area.y + area.height - 30f;
            for (int color = 0; color < 7; ++color)
            {
                string tally = BallsNative.EditorTally(session, color, out bool matches);
                if (string.IsNullOrEmpty(tally) || tally == "0/0")
                {
                    continue;
                }
                if (x > area.xMax - 50f) break;
                theme.FillDisc(new Rect(x, y + 2f, 14f, 14f), Palette.Balls[color]);
                Color c = matches ? Palette.Success : Palette.Text;
                GUI.Label(theme.Units(x + 18f, y, 120f, 20f), tally, theme.TextStyle(TypeSizes.Value, c, true, TextAnchor.MiddleLeft));
                x += 20f + tally.Length * 11f + 14f;
            }
        }

        private void DrawToolPanel(Rect area, NativeEditorState state)
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Frame(area, Palette.Panel, Palette.Border);

            Rect inner = new Rect(area.x + 12f, area.y + 12f, area.width - 24f, area.height - 24f);

            widgets.ScrollColumn(inner, toolsContentHeight, ref toolsScroll, content =>
            {
                float cursor = content.y + 8f;

                SectionTitle(content, ref cursor, "Tool");
                for (int i = 0; i < 4; ++i)
                {
                    string label = BallsNative.EditorToolLabel(i);
                    if (string.IsNullOrEmpty(label))
                    {
                        label = i switch { 0 => "Paint (P)", 1 => "Extend (E)", 2 => "Erase (X)", _ => "Pick (K)" };
                    }
                    if (widgets.AccentButton(NextControl(content, ref cursor), label, state.tool == i, TypeSizes.Header))
                    {
                        BallsNative.balls_editor_session_set_tool(session, i);
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
                    if (i == state.brushColor)
                    {
                        theme.Frame(new Rect(swatch.x - 2f, swatch.y - 2f, swatch.width + 4f, swatch.height + 4f), UiTheme.Clear, Palette.TextStrong);
                    }
                    if (GUI.Button(theme.Units(swatch.x, swatch.y, swatch.width, swatch.height), GUIContent.none, GUIStyle.none))
                    {
                        BallsNative.balls_editor_session_select_color(session, i);
                    }
                }

                SectionTitle(content, ref cursor, "Brick");
                int hits = state.brushHits;
                if (widgets.Stepper(NextControl(content, ref cursor), "Hits", ref hits, 1, 999, out _))
                {
                    BallsNative.balls_editor_session_set_brush_hits(session, hits);
                }
                string selection = BallsNative.EditorText(session, BallsNative.EditorTextSelection);
                widgets.Muted(NextControl(content, ref cursor, 24f), selection, TypeSizes.Value);

                SectionTitle(content, ref cursor, "Launcher");
                int launcherBalls = state.launcherBalls;
                if (widgets.Stepper(NextControl(content, ref cursor), "Balls", ref launcherBalls, 1, 100000, out _))
                {
                    BallsNative.balls_editor_session_set_launcher_balls(session, launcherBalls);
                }
                widgets.Muted(NextControl(content, ref cursor, 22f), "L-click set, R-click clear", TypeSizes.Small);
                widgets.Muted(NextControl(content, ref cursor, 22f), "Shift+R-click delete row", TypeSizes.Small);

                toolsContentHeight = Mathf.Max(inner.height, cursor - content.y + 16f);
            });
        }

        private void DrawPropertyPanel(Rect area, NativeEditorState state)
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Frame(area, Palette.Panel, Palette.Border);

            Rect inner = new Rect(area.x + 12f, area.y + 12f, area.width - 24f, area.height - 24f);

            widgets.ScrollColumn(inner, propertiesContentHeight, ref propertiesScroll, content =>
            {
                float cursor = content.y + 8f;

                SectionTitle(content, ref cursor, "Level");
                string levelName = BallsNative.EditorText(session, BallsNative.EditorTextName);
                if (widgets.TextField("level_name", NextControl(content, ref cursor), ref levelName, 40))
                {
                    BallsNative.balls_editor_session_set_name(session, levelName);
                }

                Rect modeRow = NextControl(content, ref cursor);
                if (widgets.AccentButton(ColumnSlice(modeRow, 0, 2, 8f), "Normal", state.mode == BallsApp.NormalMode, TypeSizes.Header))
                {
                    BallsNative.balls_editor_session_set_mode(session, BallsApp.NormalMode);
                }
                if (widgets.AccentButton(ColumnSlice(modeRow, 1, 2, 8f), "Perf", state.mode == BallsApp.PerformanceMode, TypeSizes.Header))
                {
                    BallsNative.balls_editor_session_set_mode(session, BallsApp.PerformanceMode);
                }

                SectionTitle(content, ref cursor, "Board");
                int width = state.width;
                int height = state.height;
                int playable = state.playableHeight;
                bool wChanged = widgets.Stepper(NextControl(content, ref cursor), "Width", ref width, 1, 200, out _);
                bool hChanged = widgets.Stepper(NextControl(content, ref cursor), "Brick rows", ref height, 1, 200, out _);
                bool pChanged = widgets.Stepper(NextControl(content, ref cursor), "Total rows", ref playable, height, 400, out _);
                if (wChanged || hChanged || pChanged)
                {
                    BallsNative.balls_editor_session_set_board_size(session, width, height, Mathf.Max(playable, height));
                }

                SectionTitle(content, ref cursor, "Launchers");
                int cols = state.launcherColumns;
                int vRows = state.launcherVisibleRows;
                if (widgets.Stepper(NextControl(content, ref cursor), "Columns", ref cols, 1, 8, out _))
                {
                    BallsNative.balls_editor_session_set_launcher_columns(session, cols);
                }
                if (widgets.Stepper(NextControl(content, ref cursor), "Visible rows", ref vRows, 1, 8, out _))
                {
                    BallsNative.balls_editor_session_set_visible_rows(session, vRows);
                }
                if (widgets.Button(NextControl(content, ref cursor), "Auto-fill to match bricks", TypeSizes.Header))
                {
                    BallsNative.balls_editor_session_auto_fill(session);
                }

                SectionTitle(content, ref cursor, "Validation");
                if (state.playable != 0)
                {
                    widgets.Label(NextControl(content, ref cursor, 26f), "Ready to play", TypeSizes.Header, Palette.Success, TextAnchor.MiddleLeft, true);
                }
                else
                {
                    for (int p = 0; p < state.problemCount; ++p)
                    {
                        string prob = BallsNative.EditorProblem(session, p);
                        widgets.Label(NextControl(content, ref cursor, 22f), prob, TypeSizes.Small, Palette.Danger);
                    }
                }

                float buttonsTop = content.y + inner.height - (38f + 8f) * 4f - 8f;
                cursor = Mathf.Max(cursor + 8f, buttonsTop);

                Rect undoRow = NextControl(content, ref cursor);
                if (widgets.Button(ColumnSlice(undoRow, 0, 2, 8f), "Undo", TypeSizes.Header, state.canUndo != 0))
                {
                    BallsNative.balls_editor_session_undo(session);
                }
                if (widgets.Button(ColumnSlice(undoRow, 1, 2, 8f), "Redo", TypeSizes.Header, state.canRedo != 0))
                {
                    BallsNative.balls_editor_session_redo(session);
                }

                string saveLabel = BallsNative.EditorText(session, BallsNative.EditorTextSaveLabel);
                if (string.IsNullOrEmpty(saveLabel)) saveLabel = "Save  (Ctrl+S)";
                if (widgets.Button(NextControl(content, ref cursor), saveLabel, TypeSizes.Header))
                {
                    BallsNative.balls_editor_session_save(session);
                }

                if (widgets.Button(NextControl(content, ref cursor), "Save & Play", TypeSizes.Header, state.playable != 0))
                {
                    if (BallsNative.balls_editor_session_save_and_play(session, out NativeLevelInfo level) != 0)
                    {
                        App.PlayFromEditor(state.mode, level.Name);
                    }
                }

                string backLabel = BallsNative.EditorText(session, BallsNative.EditorTextBackLabel);
                if (string.IsNullOrEmpty(backLabel)) backLabel = "Back";
                if (widgets.Button(NextControl(content, ref cursor), backLabel, TypeSizes.Header))
                {
                    if (BallsNative.balls_editor_session_back(session) != 0)
                    {
                        App.ActivateFlow(BallsNative.FlowBack);
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
