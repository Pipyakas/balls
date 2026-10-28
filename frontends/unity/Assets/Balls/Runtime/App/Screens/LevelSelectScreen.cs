using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Balls.Native;
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
        private string highlight = "";
        private float highlightUntil;

        public override void Enter()
        {
            folder = App.LevelDirectoryFor(App.Mode);
            Refresh();
        }

        public override void HandleEscape()
        {
            App.Go<MainMenuScreen>();
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
                        bool fresh = levels[index] == highlight && Time.realtimeSinceStartup < highlightUntil;
                        DrawRow(widgets, row, levels[index], fresh);
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

        private void DrawRow(Widgets widgets, Rect row, string name, bool fresh)
        {
            Rect edit = new Rect(row.xMax - 110f, row.y, 110f, row.height);
            Rect label = new Rect(row.x, row.y, row.width - 110f - 8f, row.height);
            bool clicked = fresh
                ? widgets.AccentButton(label, name, true, TypeSizes.Header)
                : widgets.Button(label, name, TypeSizes.Header);
            if (clicked)
            {
                App.StartGameplay(App.Mode, name, App.DefaultBackendKind);
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

        /// <summary>
        /// The game's Import: the OS file picker, then the core's importer, which parses every pair before copying it and
        /// reports each one. Unity has no player file dialog of its own, so the desktop's picker is asked for the paths.
        /// </summary>
        private void ImportFiles()
        {
            List<string> picked = PickLevelFiles();
            if (picked != null && picked.Count > 0)
            {
                Import(picked);
            }
        }

        /// <summary>Imports paths, toasts the core's summary and highlights the last level that landed.</summary>
        public bool Import(IReadOnlyList<string> picked)
        {
            // The control API can import before this screen has ever been shown, so the folder comes from the app.
            folder = App.LevelDirectoryFor(App.Mode);
            bool imported = BallsNative.ImportLevels(App.Mode, folder, picked, out List<BallsNative.LevelImport> results,
                out string summary, out string highlightName);
            App.Toast(summary, !imported);
            foreach (BallsNative.LevelImport result in results)
            {
                if (result.hasWarning)
                {
                    App.Toast(result.message, true);
                    break;
                }
            }
            if (!string.IsNullOrEmpty(highlightName))
            {
                highlight = highlightName;
                highlightUntil = Time.realtimeSinceStartup + 4f;
                int index = levels.IndexOf(highlightName);
                if (index >= 0)
                {
                    scroll.y = index * (RowHeight + RowGap);
                }
            }
            Refresh();
            return imported;
        }

        private List<string> PickLevelFiles()
        {
#if UNITY_EDITOR
            string single = UnityEditor.EditorUtility.OpenFilePanel("Import level files", folder, "txt");
            return string.IsNullOrEmpty(single) ? null : new List<string> { single };
#elif UNITY_STANDALONE_OSX
            string mac = RunPicker("/usr/bin/osascript", "-e", "POSIX path of (choose file with prompt \"Import level files\")");
            return string.IsNullOrEmpty(mac) ? null : new List<string> { mac.Trim() };
#elif UNITY_STANDALONE_LINUX
            string zenity = RunPicker("zenity", "--file-selection", "--multiple", "--separator=\n",
                "--title=Import level files");
            if (zenity != null)
            {
                return SplitPicked(zenity);
            }
            string kdialog = RunPicker("kdialog", "--getopenfilename", folder, "*.txt", "--multiple", "--separate-output");
            if (kdialog != null)
            {
                return SplitPicked(kdialog);
            }
            App.Toast("No file picker found; import through the control API instead", true);
            return null;
#else
            const string script =
                "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; " +
                "$d.Filter = 'Level files (*.txt)|*.txt'; $d.Multiselect = $true; " +
                "if ($d.ShowDialog() -eq 'OK') { $d.FileNames }";
            string windows = RunPicker("powershell", "-NoProfile", "-STA", "-Command", script);
            return windows == null ? null : SplitPicked(windows);
#endif
        }

        private static List<string> SplitPicked(string output)
        {
            var picked = new List<string>();
            foreach (string line in output.Split('\n'))
            {
                string path = line.Trim();
                if (path.Length > 0 && File.Exists(path))
                {
                    picked.Add(path);
                }
            }
            return picked;
        }

        /// <summary>Runs a picker command and returns what it printed, or null when it is missing or was cancelled.</summary>
        private static string RunPicker(string command, params string[] arguments)
        {
            try
            {
                var start = new ProcessStartInfo(command)
                {
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                foreach (string argument in arguments)
                {
                    start.ArgumentList.Add(argument);
                }
                using Process process = Process.Start(start);
                if (process == null)
                {
                    return null;
                }
                string output = process.StandardOutput.ReadToEnd();
                process.WaitForExit(120000);
                return process.HasExited && process.ExitCode == 0 ? output : null;
            }
            catch (System.ComponentModel.Win32Exception)
            {
                // The picker is not installed; the caller falls back to the next one.
                return null;
            }
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
