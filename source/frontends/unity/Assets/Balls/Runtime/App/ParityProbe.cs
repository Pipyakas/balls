using System;
using System.Text;
using Balls.Game;
using Balls.Native;
using Balls.Physics.Burst;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// Feeds the same balls to the native core and to the Burst port and compares where they end up.
    /// Packed positions are integers, so a difference is a real difference, not rounding.
    /// Includes first-divergent-tick tracking and controlled test modes (simple motion, wall bounce,
    /// retargeting, brick deflection, brick destruction).
    /// </summary>
    public static class ParityProbe
    {
        public enum ControlledMode
        {
            Random200,      // Standard parity probe: 200 seeded balls, 300 ticks
            SimpleMotion,   // 1 ball in empty space, 60 ticks (pure motion, no bounces)
            WallBounce,     // 1 ball hitting wall, 60 ticks, 1 bounce (bounces < 3, no retarget)
            WallRetarget,   // 1 ball bouncing 3+ times, 200 ticks, triggering retargeting
            BrickBounce,    // 1 ball deflected by non-matching brick, 60 ticks
            BrickHit,       // 1 ball destroying matching brick and dying, 60 ticks
            CornerExact,    // 1 ball hitting exact corner seam between cells (3.0, 5.0), 60 ticks
            CornerNear,     // 1 ball hitting near corner (3.002, 5.0), 60 ticks
            CornerEmbedded, // 1 ball embedded at corner diagonal tie inside cell (3, 4), 60 ticks
        }

        public struct Result
        {
            public ControlledMode mode;
            public int compared;
            public int mismatches;
            public int worstDelta;
            public int firstMismatch;
            public int sortedMismatches;
            public int firstDivergentTick;         // First tick where any delta > 0 appeared (-1 if none)
            public int firstDivergentBall;         // Ball index that first drifted
            public int firstDivergentDelta;        // Delta at first drift tick
            public string firstDivergentReason;

            public int firstBehavioralTick;        // First tick where delta exceeded tolerance (> 16 packed units, ~0.01 cell)
            public int firstBehavioralBall;
            public int firstBehavioralDelta;
            public string firstBehavioralReason;

            public int countMismatches;            // Number of ticks with alive ball count discrepancy
            public int colorMismatches;            // Number of ticks with per-color tally discrepancy
            public string error;

            public override string ToString()
            {
                var sb = new StringBuilder();
                sb.Append($"mode={mode} compared={compared} mismatches={mismatches} worstDelta={worstDelta} ");
                sb.Append($"first={firstMismatch} sorted={sortedMismatches} ");
                sb.Append($"firstDivergentTick={firstDivergentTick} ");
                if (firstBehavioralTick > 0)
                {
                    sb.Append($"firstBehavioralTick={firstBehavioralTick} (ball={firstBehavioralBall} delta={firstBehavioralDelta}) ");
                }
                sb.Append($"countMismatches={countMismatches} colorMismatches={colorMismatches} ");
                if (!string.IsNullOrEmpty(firstDivergentReason))
                {
                    sb.Append($"drift='{firstDivergentReason}' ");
                }
                if (!string.IsNullOrEmpty(firstBehavioralReason))
                {
                    sb.Append($"behavior='{firstBehavioralReason}' ");
                }
                sb.Append($"error='{error}'");
                return sb.ToString();
            }
        }

        public static Result Run(string levelDirectory, string levelName, int ballCount, int ticks)
        {
            return RunMode(levelDirectory, levelName, ControlledMode.Random200, ballCount, ticks);
        }

        public static Result RunControlled(string levelDirectory, string levelName, ControlledMode mode)
        {
            int ballCount = mode == ControlledMode.Random200 ? 200 : 1;
            int ticks = mode switch
            {
                ControlledMode.SimpleMotion => 60,
                ControlledMode.WallBounce => 60,
                ControlledMode.WallRetarget => 200,
                ControlledMode.BrickBounce => 60,
                ControlledMode.BrickHit => 60,
                ControlledMode.CornerExact => 60,
                ControlledMode.CornerNear => 60,
                ControlledMode.CornerEmbedded => 60,
                _ => 300,
            };
            return RunMode(levelDirectory, levelName, mode, ballCount, ticks);
        }

        public static Result[] RunControlledSuite(string levelDirectory, string levelName)
        {
            var modes = new[]
            {
                ControlledMode.SimpleMotion,
                ControlledMode.WallBounce,
                ControlledMode.WallRetarget,
                ControlledMode.BrickBounce,
                ControlledMode.BrickHit,
                ControlledMode.CornerExact,
                ControlledMode.CornerNear,
                ControlledMode.CornerEmbedded,
            };
            var results = new Result[modes.Length];
            for (int i = 0; i < modes.Length; ++i)
            {
                results[i] = RunControlled(levelDirectory, levelName, modes[i]);
                Debug.Log($"balls: parity [controlled] {results[i]}");
            }
            return results;
        }

        public static Result RunMode(string levelDirectory, string levelName, ControlledMode mode, int ballCount, int ticks)
        {
            var result = new Result
            {
                mode = mode,
                firstMismatch = -1,
                firstDivergentTick = -1,
                firstDivergentBall = -1,
                firstBehavioralTick = -1,
                firstBehavioralBall = -1,
                firstDivergentReason = string.Empty,
                firstBehavioralReason = string.Empty,
                error = string.Empty,
            };

            GameBackend native = null;
            GameBackend burst = null;
            try
            {
                native = new NativeGameBackend(BallsApp.NormalMode, levelDirectory, levelName, BallsNative.BackendScalar);
                burst = new BurstGameBackend(BallsApp.NormalMode, levelDirectory, levelName);
                var nativeHandle = ((NativeGameBackend)native).SessionHandle;
                var burstBackend = (BurstGameBackend)burst;

                SpawnBallsForMode(nativeHandle, burstBackend, mode, ballCount);

                int lastLoggedMilestone = 0;

                for (int tick = 0; tick < ticks; ++tick)
                {
                    BallsNative.balls_session_tick(nativeHandle, 1);
                    burstBackend.Tick(1);

                    int curNativeCount = native.ReadBalls(out IntPtr curNativePacked, out float nativeScaleX, out float nativeScaleY);
                    int curBurstCount = burst.ReadBalls(out IntPtr curBurstPacked, out float burstScaleX, out float burstScaleY);

                    bool countMismatch = curNativeCount != curBurstCount;
                    if (countMismatch)
                    {
                        result.countMismatches++;
                    }

                    int compareCount = Math.Min(curNativeCount, curBurstCount);
                    var curNativeWords = ReadWords(curNativePacked, curNativeCount);
                    var curBurstWords = ReadWords(curBurstPacked, curBurstCount);

                    bool colorMismatch = CheckColorMismatch(curNativeWords, curBurstWords, out string colorDiff);
                    if (colorMismatch)
                    {
                        result.colorMismatches++;
                    }

                    int tickSlotMismatches = 0;
                    int tickWorstDelta = 0;
                    int tickWorstBall = -1;
                    int tickFirstMismatchBall = -1;
                    int tickFirstMismatchDelta = 0;

                    for (int i = 0; i < compareCount; ++i)
                    {
                        if (curNativeWords[i] != curBurstWords[i])
                        {
                            tickSlotMismatches++;
                            int dx = Math.Abs((int)(curNativeWords[i] & 0x3FFF) - (int)(curBurstWords[i] & 0x3FFF));
                            int dy = Math.Abs((int)((curNativeWords[i] >> 14) & 0x3FFF) - (int)((curBurstWords[i] >> 14) & 0x3FFF));
                            int delta = Math.Max(dx, dy);
                            if (tickFirstMismatchBall < 0)
                            {
                                tickFirstMismatchBall = i;
                                tickFirstMismatchDelta = delta;
                            }
                            if (delta > tickWorstDelta)
                            {
                                tickWorstDelta = delta;
                                tickWorstBall = i;
                            }
                        }
                    }

                    int tickSortedMismatches = 0;
                    if (compareCount > 0)
                    {
                        var curNativeSorted = (uint[])curNativeWords.Clone();
                        var curBurstSorted = (uint[])curBurstWords.Clone();
                        Array.Sort(curNativeSorted);
                        Array.Sort(curBurstSorted);
                        for (int i = 0; i < compareCount; ++i)
                        {
                            if (curNativeSorted[i] != curBurstSorted[i])
                            {
                                tickSortedMismatches++;
                            }
                        }
                    }

                    // Milestone logging for diagnostic bisection
                    if (tickWorstDelta > lastLoggedMilestone && tickWorstBall >= 0 && tickWorstDelta >= 16)
                    {
                        lastLoggedMilestone = tickWorstDelta;
                        BallsNative.UnpackBall(curNativeWords[tickWorstBall], nativeScaleX, nativeScaleY, out float nx, out float ny, out int nc);
                        BallsNative.UnpackBall(curBurstWords[tickWorstBall], burstScaleX, burstScaleY, out float bx, out float by, out int bc);
                        Debug.Log($"balls: parity milestone [{mode}] tick={tick + 1} worstDelta={tickWorstDelta} ball={tickWorstBall} native=({nx:F3},{ny:F3}) burst=({bx:F3},{by:F3})");
                    }

                    if (result.firstDivergentTick < 0)
                    {
                        if (countMismatch)
                        {
                            result.firstDivergentTick = tick + 1;
                            result.firstDivergentReason = $"tick {tick + 1}: count mismatch native={curNativeCount} burst={curBurstCount}";
                        }
                        else if (colorMismatch)
                        {
                            result.firstDivergentTick = tick + 1;
                            result.firstDivergentReason = $"tick {tick + 1}: color mismatch {colorDiff}";
                        }
                        else if (tickSlotMismatches > 0)
                        {
                            result.firstDivergentTick = tick + 1;
                            result.firstDivergentBall = tickFirstMismatchBall;
                            result.firstDivergentDelta = tickFirstMismatchDelta;
                            result.firstDivergentReason = $"tick {tick + 1}: ball {tickFirstMismatchBall} delta={tickFirstMismatchDelta} (sortedDiffs={tickSortedMismatches})";
                        }
                    }

                    // Behavioral divergence threshold: delta > 16 packed units (~0.007 cells), which exceeds float rounding
                    if (result.firstBehavioralTick < 0 && (countMismatch || colorMismatch || tickWorstDelta > 16))
                    {
                        result.firstBehavioralTick = tick + 1;
                        result.firstBehavioralBall = countMismatch || colorMismatch ? -1 : tickWorstBall;
                        result.firstBehavioralDelta = tickWorstDelta;
                        result.firstBehavioralReason = countMismatch
                            ? $"tick {tick + 1}: count mismatch native={curNativeCount} burst={curBurstCount}"
                            : (colorMismatch
                                ? $"tick {tick + 1}: color mismatch {colorDiff}"
                                : $"tick {tick + 1}: ball {tickWorstBall} delta={tickWorstDelta}");
                    }
                }

                // Final comparison at end of simulation
                int finalNativeCount = native.ReadBalls(out IntPtr nativePacked, out _, out _);
                int finalBurstCount = burst.ReadBalls(out IntPtr burstPacked, out _, out _);
                if (finalNativeCount != finalBurstCount)
                {
                    result.error = $"count differs: native {finalNativeCount}, burst {finalBurstCount}";
                }
                int finalCompareCount = Math.Min(finalNativeCount, finalBurstCount);

                var nativeWords = ReadWords(nativePacked, finalNativeCount);
                var burstWords = ReadWords(burstPacked, finalBurstCount);

                var nativeSorted = (uint[])nativeWords.Clone();
                var burstSorted = (uint[])burstWords.Clone();
                Array.Sort(nativeSorted);
                Array.Sort(burstSorted);
                for (int index = 0; index < finalCompareCount; ++index)
                {
                    if (nativeSorted[index] != burstSorted[index])
                    {
                        result.sortedMismatches++;
                    }
                }

                for (int index = 0; index < finalCompareCount; ++index)
                {
                    result.compared++;
                    if (nativeWords[index] == burstWords[index])
                    {
                        continue;
                    }
                    result.mismatches++;
                    if (result.firstMismatch < 0)
                    {
                        result.firstMismatch = index;
                    }
                    int dx = Math.Abs((int)(nativeWords[index] & 0x3FFF) - (int)(burstWords[index] & 0x3FFF));
                    int dy = Math.Abs((int)((nativeWords[index] >> 14) & 0x3FFF) - (int)((burstWords[index] >> 14) & 0x3FFF));
                    result.worstDelta = Math.Max(result.worstDelta, Math.Max(dx, dy));
                }
            }
            catch (Exception error)
            {
                result.error = error.Message;
            }
            finally
            {
                native?.Dispose();
                burst?.Dispose();
            }
            return result;
        }

        private static void SpawnBallsForMode(IntPtr nativeHandle, BurstGameBackend burstBackend, ControlledMode mode, int ballCount)
        {
            switch (mode)
            {
                case ControlledMode.SimpleMotion:
                    // 1 ball in lower clear space (rows 5..13 are empty), moving gently downwards
                    // x=3.5, y=9.0, vx=1.0, vy=1.0, color 0 (Red). Over 60 ticks it stays in empty space.
                    SpawnOne(nativeHandle, burstBackend, 3.5f, 9.0f, 1.0f, 1.0f, 0);
                    break;

                case ControlledMode.WallBounce:
                    // 1 ball aimed at left wall (x=radius=0.2), 0.8 distance at 6 units/sec takes ~16 ticks to bounce
                    // Bounces once off left wall, reverses vx to +6.0, no retargeting (< 3 bounces)
                    SpawnOne(nativeHandle, burstBackend, 1.0f, 9.0f, -6.0f, 0.0f, 0);
                    break;

                case ControlledMode.WallRetarget:
                    // 1 ball moving horizontally at full speed (14.0f).
                    // Across width 7 (usable span 6.6), hits walls and accumulates 3+ bounces to trigger retargeting.
                    SpawnOne(nativeHandle, burstBackend, 1.0f, 9.0f, -14.0f, 0.0f, 0);
                    break;

                case ControlledMode.BrickBounce:
                    // In DA_FirstLevel, cell at (col 3, row 4) is Green (color 3).
                    // Spawn Red (color 0) ball below it aimed upwards.
                    // Red != Green, so the ball bounces off the brick boundary without destroying it.
                    SpawnOne(nativeHandle, burstBackend, 3.5f, 6.0f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.BrickHit:
                    // Cell at (col 3, row 4) is Green (color 3).
                    // Spawn Green (color 3) ball aimed upwards at it.
                    // Matching color destroys the brick and kills the ball.
                    SpawnOne(nativeHandle, burstBackend, 3.5f, 6.0f, 0.0f, -9.0f, 3);
                    break;

                case ControlledMode.CornerExact:
                    // Seam between col 2 (Blue) and col 3 (Green) at row 4 bottom boundary (y=5.0).
                    // Ball aimed straight up at exact x=3.0f, y=5.6f. Hits corner vertex exactly.
                    SpawnOne(nativeHandle, burstBackend, 3.0f, 5.6f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.CornerNear:
                    // Ball slightly offset from seam at x=3.002f, y=5.6f aimed straight up.
                    SpawnOne(nativeHandle, burstBackend, 3.002f, 5.6f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.CornerEmbedded:
                    // Ball starting embedded inside cell (3, 4) at exact diagonal tie distance (0.05 from left and bottom).
                    // x=3.05f, y=4.95f -> toLeft = 0.05f, toBottom = 0.05f.
                    // Tests pushOutOfCell tie resolution parity directly.
                    SpawnOne(nativeHandle, burstBackend, 3.05f, 4.95f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.Random200:
                default:
                    var random = new System.Random(20261028);
                    for (int index = 0; index < ballCount; ++index)
                    {
                        float x = 0.6f + (float)random.NextDouble() * 5.8f;
                        float y = 0.6f + (float)random.NextDouble() * 12.8f;
                        float angle = (float)random.NextDouble() * 6.2831853f;
                        float vx = (float)Math.Cos(angle) * 9f;
                        float vy = (float)Math.Sin(angle) * 9f;
                        int color = index % 3;
                        SpawnOne(nativeHandle, burstBackend, x, y, vx, vy, color);
                    }
                    break;
            }
        }

        private static void SpawnOne(IntPtr nativeHandle, BurstGameBackend burstBackend, float x, float y, float vx, float vy, int color)
        {
            BallsNative.balls_session_spawn_ball(nativeHandle, x, y, vx, vy, color);
            burstBackend.SpawnBallExact(x, y, vx, vy, color);
        }

        private static bool CheckColorMismatch(uint[] nativeWords, uint[] burstWords, out string diff)
        {
            int[] n = new int[7];
            int[] b = new int[7];
            for (int i = 0; i < nativeWords.Length; ++i)
            {
                int c = (int)((nativeWords[i] >> BallsNative.RenderColorShift) & 0x7);
                if (c >= 0 && c < 7) n[c]++;
            }
            for (int i = 0; i < burstWords.Length; ++i)
            {
                int c = (int)((burstWords[i] >> BallsNative.RenderColorShift) & 0x7);
                if (c >= 0 && c < 7) b[c]++;
            }
            for (int i = 0; i < 7; ++i)
            {
                if (n[i] != b[i])
                {
                    diff = $"c{i}: native={n[i]} burst={b[i]}";
                    return true;
                }
            }
            diff = string.Empty;
            return false;
        }

        private static unsafe uint[] ReadWords(IntPtr pointer, int count)
        {
            var words = new uint[count];
            if (pointer == IntPtr.Zero || count == 0)
            {
                return words;
            }
            var source = (uint*)pointer;
            for (int index = 0; index < count; ++index)
            {
                words[index] = source[index];
            }
            return words;
        }
    }
}
