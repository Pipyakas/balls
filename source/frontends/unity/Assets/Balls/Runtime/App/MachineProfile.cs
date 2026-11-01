using System;
using System.IO;
using Balls.Native;
using UnityEngine;

namespace Balls.App
{

    public sealed class MachineProfile
    {
        private const string TargetFileName = "target-benchmark.txt";

        private IntPtr handle;

        internal IntPtr Handle => handle;

        public static MachineProfile Load()
        {
            var profile = new MachineProfile { handle = BallsNative.balls_profile_open(Application.persistentDataPath) };
            if (profile.handle == IntPtr.Zero)
            {
                Debug.LogWarning($"balls: cannot open the machine profile: {BallsNative.LastError()}");
            }
            return profile;
        }

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

        public bool TryScore(int backendKind, out double ballStepsPerSecond) =>
            BallsNative.balls_profile_benchmark_score(handle, backendKind, out ballStepsPerSecond) != 0;

        public bool TryLimit(int backendKind, out ulong balls, out double fps) =>
            BallsNative.balls_profile_ball_limit(handle, backendKind, out fps, out balls) != 0;

        public void SetLimit(int backendKind, ulong balls, double fps) =>
            BallsNative.balls_profile_remember_ball_limit(handle, backendKind, fps, balls);

        public void RememberBenchmark(IntPtr benchmark, bool complete) =>
            BallsNative.balls_profile_remember_benchmark(handle, benchmark, complete ? 1 : 0, Application.version);
    }
}
