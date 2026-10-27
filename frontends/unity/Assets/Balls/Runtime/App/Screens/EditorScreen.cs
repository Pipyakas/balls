using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The map editor. The core owns the document (paint, extend, erase, launcher rows, undo/redo, validation, save),
    /// so this screen is the grid, the toolbars and the property panel around it.
    ///
    /// Parity slice S5. The document plumbing below is already wired to the ABI; the grid and
    /// tool interaction are the part still to come.
    /// </summary>
    public sealed class EditorScreen : Screen
    {
        private System.IntPtr editor;
        private string levelName = "NewLevel";
        private int mode = BallsApp.NormalMode;
        private string status = "";

        public override void Enter()
        {
            if (editor == System.IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
                BallsNative.balls_editor_new_level(editor, levelName, 10, 8, 20, 3, 3);
            }
        }

        public void StartNewLevel(int forMode)
        {
            mode = forMode;
            levelName = "NewLevel";
            if (editor != System.IntPtr.Zero)
            {
                BallsNative.balls_editor_new_level(editor, levelName, 10, 8, 20, 3, 3);
            }
            status = "new level";
        }

        public void StartEditing(int forMode, string name)
        {
            mode = forMode;
            levelName = name;
            if (editor == System.IntPtr.Zero)
            {
                editor = BallsNative.balls_editor_create();
            }
            string directory = App.LevelDirectoryFor(forMode);
            int result = BallsNative.balls_editor_load_level(editor, directory, name);
            status = result == 0 ? $"loaded {name}" : $"cannot load {name}: {BallsNative.LastError()}";
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            BallsNative.balls_editor_info(editor, out NativeEditorInfo info);
            widgets.Label(new Rect(0f, 32f, theme.UnitsWidth, 52f), $"Map Editor · {levelName}", TypeSizes.Title,
                Palette.Accent, TextAnchor.MiddleCenter, true);
            widgets.Muted(new Rect(0f, 96f, theme.UnitsWidth, 24f),
                $"board {info.width}x{info.height} · brick rows {info.playableHeight} · launchers {info.launcherColumns}x{info.launcherVisibleRows}",
                TypeSizes.Small, TextAnchor.MiddleCenter);

            Rect panel = new Rect((theme.UnitsWidth - 560f) * 0.5f, theme.UnitsHeight * 0.28f, 560f, 220f);
            theme.Frame(panel, Palette.Panel, Palette.Border);
            widgets.Label(new Rect(panel.x + 20f, panel.y + 16f, panel.width - 40f, 28f),
                "The editor grid and tools are the next slice.", TypeSizes.Medium, Palette.Text);
            widgets.Muted(new Rect(panel.x + 20f, panel.y + 52f, panel.width - 40f, 24f),
                "Document plumbing (load, paint, validate, save) is wired to the core; the grid is not drawn yet.",
                TypeSizes.Small);
            if (!string.IsNullOrEmpty(status))
            {
                widgets.Muted(new Rect(panel.x + 20f, panel.y + 86f, panel.width - 40f, 24f), status, TypeSizes.Small);
            }

            if (widgets.Button(new Rect(panel.x + 20f, panel.yMax - 70f, 180f, 52f), "Back"))
            {
                App.Go<MainMenuScreen>();
            }
        }
    }
}
