using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// The game's machine profile: the last benchmark score of every backend and the capacity probe's ball limit per
    /// backend, plus the hardware fingerprint they were measured on. Stored as tab-separated lines in the same style as
    /// the shipped data/assets/target-benchmark.txt.
    /// </summary>
    public sealed class MachineProfile
    {
        private const string FileName = "machine-profile.txt";
        private const string TargetFileName = "target-benchmark.txt";

        private readonly Dictionary<string, double> scores = new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, (ulong balls, double fps)> limits = new(StringComparer.OrdinalIgnoreCase);

        public static string ProfilePath => Path.Combine(Application.persistentDataPath, FileName);

        public static MachineProfile Load()
        {
            var profile = new MachineProfile();
            try
            {
                if (File.Exists(ProfilePath))
                {
                    profile.Parse(File.ReadAllLines(ProfilePath));
                }
            }
            catch (IOException error)
            {
                Debug.LogWarning($"balls: cannot read {ProfilePath}: {error.Message}");
            }
            return profile;
        }

        /// <summary>The backend that won on the target PC, from the record shipped beside the levels, or null.</summary>
        public static string TargetWinner()
        {
            try
            {
                string path = Path.Combine(Application.streamingAssetsPath, TargetFileName);
                if (!File.Exists(path))
                {
                    return null;
                }
                foreach (string line in File.ReadAllLines(path))
                {
                    string[] parts = line.Split('\t');
                    if (parts.Length >= 2 && parts[0] == "winner" && parts[1].Length > 0)
                    {
                        return parts[1];
                    }
                }
            }
            catch (IOException error)
            {
                Debug.LogWarning($"balls: cannot read {TargetFileName}: {error.Message}");
            }
            return null;
        }

        public bool TryScore(string backend, out double ballStepsPerSecond) => scores.TryGetValue(backend, out ballStepsPerSecond);

        public bool TryLimit(string backend, out ulong balls, out double fps)
        {
            bool found = limits.TryGetValue(backend, out var limit);
            balls = limit.balls;
            fps = limit.fps;
            return found;
        }

        public void SetScore(string backend, double ballStepsPerSecond)
        {
            scores[backend] = ballStepsPerSecond;
        }

        public void SetLimit(string backend, ulong balls, double fps)
        {
            limits[backend] = (balls, fps);
        }

        public void Save()
        {
            var text = new StringBuilder();
            text.Append("cpu\t").Append(SystemInfo.processorType).Append('\n');
            text.Append("cores\t").Append(SystemInfo.processorCount).Append('\n');
            text.Append("gpu\t").Append(SystemInfo.graphicsDeviceName).Append(" | ").Append(SystemInfo.graphicsDeviceVersion)
                .Append('\n');
            text.Append("version\t").Append(Application.version).Append('\n');
            foreach (var score in scores)
            {
                text.Append("score\t").Append(score.Key).Append('\t')
                    .Append(score.Value.ToString("F0", CultureInfo.InvariantCulture)).Append('\n');
            }
            foreach (var limit in limits)
            {
                text.Append("limit\t").Append(limit.Key).Append('\t').Append(limit.Value.balls).Append('\t')
                    .Append(limit.Value.fps.ToString("F1", CultureInfo.InvariantCulture)).Append('\n');
            }
            try
            {
                Directory.CreateDirectory(Application.persistentDataPath);
                File.WriteAllText(ProfilePath, text.ToString());
            }
            catch (IOException error)
            {
                Debug.LogWarning($"balls: cannot write {ProfilePath}: {error.Message}");
            }
        }

        private void Parse(string[] lines)
        {
            foreach (string line in lines)
            {
                string[] parts = line.Split('\t');
                if (parts.Length >= 3 && parts[0] == "score" &&
                    double.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out double score))
                {
                    scores[parts[1]] = score;
                }
                else if (parts.Length >= 4 && parts[0] == "limit" && ulong.TryParse(parts[2], out ulong balls) &&
                         double.TryParse(parts[3], NumberStyles.Float, CultureInfo.InvariantCulture, out double fps))
                {
                    limits[parts[1]] = (balls, fps);
                }
            }
        }
    }
}
