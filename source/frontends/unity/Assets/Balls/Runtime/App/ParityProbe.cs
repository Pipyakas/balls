using System;
using System.Text;
using Balls.Game;
using Balls.Native;
using Balls.Physics.Burst;
using UnityEngine;

namespace Balls.App
{

    public static class ParityProbe
    {
        public enum ControlledMode
        {
            Random200,
            SimpleMotion,
            WallBounce,
            WallRetarget,
            BrickBounce,
            BrickHit,
            CornerExact,
            CornerNear,
            CornerEmbedded,
        }

        public struct Result
        {
            public ControlledMode mode;
            public int compared;
            public int mismatches;
            public int worstDelta;
            public int firstMismatch;
            public int sortedMismatches;
            public int firstDivergentTick;
            public int firstDivergentBall;
            public int firstDivergentDelta;
            public string firstDivergentReason;

            public int firstBehavioralTick;
            public int firstBehavioralBall;
            public int firstBehavioralDelta;
            public string firstBehavioralReason;

            public int countMismatches;
            public int colorMismatches;
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

                    SpawnOne(nativeHandle, burstBackend, 3.5f, 9.0f, 1.0f, 1.0f, 0);
                    break;

                case ControlledMode.WallBounce:

                    SpawnOne(nativeHandle, burstBackend, 1.0f, 9.0f, -6.0f, 0.0f, 0);
                    break;

                case ControlledMode.WallRetarget:

                    SpawnOne(nativeHandle, burstBackend, 1.0f, 9.0f, -14.0f, 0.0f, 0);
                    break;

                case ControlledMode.BrickBounce:

                    SpawnOne(nativeHandle, burstBackend, 3.5f, 6.0f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.BrickHit:

                    SpawnOne(nativeHandle, burstBackend, 3.5f, 6.0f, 0.0f, -9.0f, 3);
                    break;

                case ControlledMode.CornerExact:

                    SpawnOne(nativeHandle, burstBackend, 3.0f, 5.6f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.CornerNear:

                    SpawnOne(nativeHandle, burstBackend, 3.002f, 5.6f, 0.0f, -9.0f, 0);
                    break;

                case ControlledMode.CornerEmbedded:

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
