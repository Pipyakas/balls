using System;
using Balls.Game;
using Balls.Native;
using Balls.Physics.Burst;

namespace Balls.App
{
    /// <summary>
    /// Feeds the same balls to the native core and to the Burst port and compares where they end up. Packed positions
    /// are integers, so a difference is a real difference, not rounding: the two implementations either agree bit for
    /// bit or one of them is wrong.
    /// </summary>
    public static class ParityProbe
    {
        public struct Result
        {
            public int compared;
            public int mismatches;
            public int worstDelta;
            public int firstMismatch;
            public string error;
        }

        public static Result Run(string levelDirectory, string levelName, int ballCount, int ticks)
        {
            var result = new Result { firstMismatch = -1 };
            GameBackend native = null;
            GameBackend burst = null;
            try
            {
                native = new NativeGameBackend(BallsApp.NormalMode, levelDirectory, levelName, BallsNative.BackendScalar);
                burst = new BurstGameBackend(BallsApp.NormalMode, levelDirectory, levelName);
                var nativeHandle = ((NativeGameBackend)native).SessionHandle;
                var burstBackend = (BurstGameBackend)burst;

                var random = new Random(20261028);
                for (int index = 0; index < ballCount; ++index)
                {
                    // Inside the field, away from the walls, so no clamping differences hide a step difference.
                    float x = 0.6f + (float)random.NextDouble() * 5.8f;
                    float y = 0.6f + (float)random.NextDouble() * 12.8f;
                    float angle = (float)random.NextDouble() * 6.2831853f;
                    float vx = (float)Math.Cos(angle) * 9f;
                    float vy = (float)Math.Sin(angle) * 9f;
                    int color = index % 3;
                    BallsNative.balls_session_spawn_ball(nativeHandle, x, y, vx, vy, color);
                    burstBackend.SpawnBallExact(x, y, vx, vy, color);
                }

                for (int tick = 0; tick < ticks; ++tick)
                {
                    BallsNative.balls_session_tick(nativeHandle, 1);
                    burstBackend.Tick(1);
                }

                int nativeCount = native.ReadBalls(out IntPtr nativePacked, out _, out _);
                int burstCount = burst.ReadBalls(out IntPtr burstPacked, out _, out _);
                if (nativeCount != burstCount)
                {
                    result.error = $"count differs: native {nativeCount}, burst {burstCount}";
                    return result;
                }

                var nativeWords = ReadWords(nativePacked, nativeCount);
                var burstWords = ReadWords(burstPacked, burstCount);
                for (int index = 0; index < nativeCount; ++index)
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
