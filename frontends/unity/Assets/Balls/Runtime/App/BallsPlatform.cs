using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// Platform integration helpers for desktop OS dialogs and shell commands.
    /// </summary>
    public static class BallsPlatform
    {
        public static List<string> PickLevelFiles(string folder)
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
                return null;
            }
        }

        public static void OpenFolder(string folder)
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
                UnityEngine.Debug.LogWarning($"cannot open {folder}: {error.Message}");
            }
        }
    }
}
