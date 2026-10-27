using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's level select: the mode's folder listed live, with Import, Open Folder, Edit, New Level and Back.
    /// </summary>
    public sealed class LevelSelectScreen : Screen
    {
        private const float RowHeight = 58f;
        private const float RowGap = 12f;
        private const float ListWidth = 560f;

        private readonly List<string> levels = new();
        private Vector2 scroll;
        private float nextRefresh;
        private string folder = "";

        public override void Enter()
        {
            folder = App.LevelDirectoryFor(App.Mode);
            Refresh();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            float width = theme.UnitsWidth;
            float height = theme.UnitsHeight;
            theme.Fill(new Rect(0f, 0f, width, height), Palette.Background);

            if (Time.realtimeSinceStartup >= nextRefresh)
            {
                Refresh();
            }

            string title = App.Mode == BallsApp.PerformanceMode ? "Performance Mode" : "Normal Mode";
            widgets.Label(new Rect(0f, 48f, width, 60f), title, TypeSizes.Title, Palette.Accent,
                TextAnchor.MiddleCenter, true);

            float listWidth = Mathf.Min(ListWidth, width - 64f);
            float listX = (width - listWidth) * 0.5f;
            float listTop = 150f;
            float listHeight = Mathf.Min(levels.Count * (RowHeight + RowGap), height - listTop - 240f);
            Rect viewport = new Rect(listX, listTop, listWidth, Mathf.Max(listHeight, RowHeight));

            if (levels.Count == 0)
            {
                widgets.Muted(new Rect(listX, listTop + 20f, listWidth, 32f), "No levels found", TypeSizes.Big,
                    TextAnchor.MiddleCenter);
            }
            else
            {
                Rect content = new Rect(viewport.x, viewport.y - scroll.y, viewport.width,
                    levels.Count * (RowHeight + RowGap));
                scroll.y = Mathf.Clamp(scroll.y, 0f, Mathf.Max(0f, content.height - viewport.height));
                for (int index = 0; index < levels.Count; ++index)
                {
                    Rect row = new Rect(content.x, content.y + index * (RowHeight + RowGap), content.width, RowHeight);
                    if (row.yMax < viewport.y - RowHeight || row.y < viewport.y + viewport.height)
                    {
                        DrawRow(widgets, row, levels[index]);
                    }
                }
            }

            float buttonY = Mathf.Min(height - 150f, listTop + viewport.height + 24f);
            float buttonWidth = 200f;
            float gap = 12f;
            float total = buttonWidth * 4f + gap * 3f;
            float startX = (width - total) * 0.5f;

            if (widgets.Button(new Rect(startX, buttonY, buttonWidth, 56f), "Back"))
            {
                App.Go<MainMenuScreen>();
            }
            if (widgets.Button(new Rect(startX + (buttonWidth + gap), buttonY, buttonWidth, 56f), "New Level"))
            {
                App.Get<EditorScreen>().StartNewLevel(App.Mode);
                App.Go<EditorScreen>();
            }
            if (widgets.Button(new Rect(startX + (buttonWidth + gap) * 2f, buttonY, buttonWidth, 56f), "Import"))
            {
                ImportFiles();
            }
            if (widgets.Button(new Rect(startX + (buttonWidth + gap) * 3f, buttonY, buttonWidth, 56f), "Open Folder"))
            {
                OpenFolder();
            }

            widgets.Muted(new Rect(0f, buttonY + 72f, width, 24f), $"Levels folder: {folder}",
                TypeSizes.Small, TextAnchor.MiddleCenter);
        }

        private void DrawRow(Widgets widgets, Rect row, string name)
        {
            Rect edit = new Rect(row.xMax - 110f, row.y, 110f, row.height);
            Rect label = new Rect(row.x, row.y, row.width - 110f - 8f, row.height);
            if (widgets.Button(label, name, TypeSizes.Header))
            {
                App.StartGameplay(App.Mode, name, App.BackendKind);
            }
            if (widgets.Button(edit, "Edit", TypeSizes.Label))
            {
                App.Get<EditorScreen>().StartEditing(App.Mode, name);
                App.Go<EditorScreen>();
            }
        }

        private void Refresh()
        {
            nextRefresh = Time.realtimeSinceStartup + 0.5f;
            levels.Clear();
            levels.AddRange(App.LevelNames());
        }

        private void ImportFiles()
        {
#if UNITY_EDITOR
            string picked = UnityEditor.EditorUtility.OpenFilePanel("Import level files", folder, "txt");
            if (string.IsNullOrEmpty(picked))
            {
                return;
            }
            string name = Path.GetFileNameWithoutExtension(picked);
            string stem = name.EndsWith("_Bricks")
                ? name.Substring(0, name.Length - "_Bricks".Length)
                : name.EndsWith("_SpawnBall") ? name.Substring(0, name.Length - "_SpawnBall".Length) : name;
            string bricks = Path.Combine(Path.GetDirectoryName(picked) ?? folder, stem + "_Bricks.txt");
            string launchers = Path.Combine(Path.GetDirectoryName(picked) ?? folder, stem + "_SpawnBall.txt");
            if (!File.Exists(bricks) || !File.Exists(launchers))
            {
                App.Toast($"Import failed: {stem} is missing its other file", true);
                return;
            }
            Directory.CreateDirectory(folder);
            File.Copy(bricks, Path.Combine(folder, Path.GetFileName(bricks)), true);
            File.Copy(launchers, Path.Combine(folder, Path.GetFileName(launchers)), true);
            App.Toast($"Imported {stem}");
            Refresh();
#else
            App.Toast("Open Folder, then copy the level pair into it", true);
#endif
        }

        private void OpenFolder()
        {
            try
            {
                Directory.CreateDirectory(folder);
#if UNITY_STANDALONE_LINUX
                Process.Start("xdg-open", folder);
#elif UNITY_STANDALONE_OSX
                Process.Start("open", folder);
#else
                Process.Start("explorer.exe", folder.Replace('/', '\\'));
#endif
            }
            catch (System.Exception error)
            {
                App.Toast($"cannot open {folder}: {error.Message}", true);
            }
        }
    }
}
