using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Balls.Ecs;
using Balls.Native;

namespace Balls.Game
{
    /// <summary>
    /// A session run by the C++ core through balls_unity.dll. Physics stays in the core, so this backend can use the
    /// AVX2, CUDA and Vulkan backends the other front ends already use.
    /// </summary>
    public sealed class NativeGameBackend : GameBackend
    {
        private readonly IntPtr session;
        private BoardFrame frame;
        private BackendStats stats;

        /// <summary>
        /// Runs the session on the core's physics thread (SessionRunner) instead of inside Update, so a long physics
        /// step overlaps rendering instead of adding to the frame. Set from <c>-balls-physics-thread</c>.
        /// </summary>
        public static bool UsePhysicsThread { get; set; }

        public NativeGameBackend(int mode, string levelDirectory, string levelName, int backendKind)
        {
            session = UsePhysicsThread
                ? BallsNative.balls_session_create_threaded(mode, levelDirectory, levelName, backendKind)
                : BallsNative.balls_session_create(mode, levelDirectory, levelName, backendKind);
            if (session == IntPtr.Zero)
            {
                throw new InvalidOperationException($"cannot open level {levelName}: {BallsNative.LastError()}");
            }
            RefreshFrame();
        }

        public override bool IsNative => true;

        /// <summary>The ABI handle, for the few calls the interface does not model yet.</summary>
        public System.IntPtr SessionHandle => session;

        public override int Mode => BallsNative.balls_session_mode(session);

        public override int State => BallsNative.balls_session_state(session);

        public override int BackendKind => BallsNative.balls_session_backend(session);

        public override double Elapsed => BallsNative.balls_session_elapsed_seconds(session);

        public override BoardFrame Frame => frame;

        public override BackendStats Stats => stats;

        public override string LastFault
        {
            get
            {
                var buffer = new byte[256];
                int length = BallsNative.balls_session_last_backend_fault(session, buffer, buffer.Length);
                return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
            }
        }

        public override void Update(float deltaSeconds)
        {
            BallsNative.balls_session_update(session, deltaSeconds);
            RefreshFrame();
            BallsNative.balls_session_stats(session, out var native);
            stats = new BackendStats
            {
                ballCount = native.ballCount,
                liveBallCount = native.liveBallCount,
                spawned = native.totalBallsSpawned,
                hits = native.totalHits,
                physicsMs = native.lastFramePhysicsMs,
                ticks = native.ticksLastFrame,
                red = native.ballsByColor0,
                orange = native.ballsByColor1,
                yellow = native.ballsByColor2,
                green = native.ballsByColor3,
                blue = native.ballsByColor4,
                indigo = native.ballsByColor5,
                violet = native.ballsByColor6,
            };
        }

        public override void Restart()
        {
            BallsNative.balls_session_restart(session);
            RefreshFrame();
        }

        public override bool ChooseLauncher(int column, int row) => BallsNative.balls_session_choose_launcher(session, column, row) != 0;

        public override void StartAutoRelease() => BallsNative.balls_session_start_auto_release(session);

        public override void SelectSpawnRate(int index) => BallsNative.balls_session_select_spawn_rate(session, index);

        public override void SelectBallMultiplier(int index) => BallsNative.balls_session_select_ball_multiplier(session, index);

        public override void SelectBallLimit(int index) => BallsNative.balls_session_select_ball_limit(session, index);

        public override bool SwitchBackend(int kind) => BallsNative.balls_session_switch_backend(session, kind) != 0;

        public override int PendingBackend => BallsNative.balls_session_pending_backend(session);

        public override int TakeSwitchOutcome(out int kind) => BallsNative.balls_session_take_switch_outcome(session, out kind);

        public override void StartProbe(double targetFps) => BallsNative.balls_session_start_capacity_probe(session, targetFps);

        public override void ReportFrame(double frameSeconds, double workSeconds) => BallsNative.balls_session_report_frame(session, frameSeconds, workSeconds);

        public override int SpawnRateIndex => BallsNative.balls_session_spawn_rate_index(session);

        public override int BallMultiplierIndex => BallsNative.balls_session_ball_multiplier_index(session);

        public override int BallLimitIndex => BallsNative.balls_session_ball_limit_index(session);

        public override ulong BallLimit => BallsNative.balls_session_ball_limit(session);

        public override bool ProbeActive
        {
            get
            {
                BallsNative.balls_session_probe(session, out var probe);
                return probe.active != 0;
            }
        }

        public override int ProbePhase
        {
            get
            {
                BallsNative.balls_session_probe(session, out var probe);
                return probe.phase;
            }
        }

        public override double ProbeFps
        {
            get
            {
                BallsNative.balls_session_probe(session, out var probe);
                return probe.lastFps;
            }
        }

        public override double ProbeTargetFps
        {
            get
            {
                BallsNative.balls_session_probe(session, out var probe);
                return probe.targetFps;
            }
        }

        public override ulong ProbeBest
        {
            get
            {
                BallsNative.balls_session_probe(session, out var probe);
                return probe.bestPassingCount;
            }
        }

        public override bool HasMachineLimit => BallsNative.balls_session_has_machine_ball_limit(session) != 0;

        public override ulong MachineLimit => BallsNative.balls_session_machine_ball_limit(session);

        public override bool BallCollisions => BallsNative.balls_session_ball_collisions(session) != 0;

        public override int SetBallCollisions(bool enabled) => BallsNative.balls_session_set_ball_collisions(session, enabled ? 1 : 0);

        public override int TakeCollisionsChange() => BallsNative.balls_session_take_collisions_change(session);

        public override double PhysicsThreadFps =>
            BallsNative.balls_session_threaded(session) != 0 ? BallsNative.balls_session_physics_fps(session) : -1.0;

        public override void CancelProbe() => BallsNative.balls_session_cancel_capacity_probe(session);

        public override void UseMachineLimit(ulong limit) => BallsNative.balls_session_use_machine_ball_limit(session, limit);

        public override void SetVisibilityGrid(int width, int height)
        {
            BallsNative.balls_session_set_visibility_grid(session, width, height);
        }

        public override void ReadCells(List<CellElement> destination)
        {
            BallsNative.balls_session_board_view(session, out var view);
            int count = frame.paddedCellCount;
            while (destination.Count < count)
            {
                destination.Add(default);
            }
            if (destination.Count > count)
            {
                destination.RemoveRange(count, destination.Count - count);
            }
            if (count == 0)
            {
                return;
            }
            Span<int> cells = MemoryMarshal.Cast<byte, int>(ReadArray(view.paddedCells, count, sizeof(int)));
            int brickCount = view.brickCount;
            Span<int> colors = MemoryMarshal.Cast<byte, int>(ReadArray(view.brickColors, brickCount, sizeof(int)));
            Span<int> hits = MemoryMarshal.Cast<byte, int>(ReadArray(view.brickHitsLeft, brickCount, sizeof(int)));
            Span<float> rise = MemoryMarshal.Cast<byte, float>(ReadArray(view.brickRiseOffset, brickCount, sizeof(float)));
            Span<float> flash = MemoryMarshal.Cast<byte, float>(ReadArray(view.brickHitFlash, brickCount, sizeof(float)));
            for (int index = 0; index < count; ++index)
            {
                int brick = cells[index];
                bool occupied = brick >= 0 && brick < brickCount;
                destination[index] = new CellElement
                {
                    brick = brick,
                    color = occupied ? colors[brick] : 0,
                    hitsLeft = occupied ? hits[brick] : 0,
                    rise = occupied ? rise[brick] : 0f,
                    flash = occupied ? flash[brick] : 0f,
                };
            }
        }

        public override void ReadLaunchers(List<LauncherElement> destination)
        {
            int count = frame.launcherColumns * Math.Max(1, frame.launcherVisibleRows);
            while (destination.Count < count)
            {
                destination.Add(default);
            }
            if (destination.Count > count)
            {
                destination.RemoveRange(count, destination.Count - count);
            }
            int slot = 0;
            for (int row = 0; row < frame.launcherVisibleRows; ++row)
            {
                for (int column = 0; column < frame.launcherColumns; ++column, ++slot)
                {
                    BallsNative.balls_session_launcher(session, column, row, out var launcher);
                    destination[slot] = new LauncherElement
                    {
                        column = column,
                        row = row,
                        color = launcher.color,
                        ballCount = launcher.ballCount,
                        rise = launcher.riseOffset,
                        present = launcher.present != 0,
                    };
                }
            }
        }

        public override int ReadBalls(out IntPtr packed, out float scaleX, out float scaleY)
        {
            if (BallsNative.balls_session_snapshot(session, out var snapshot) == 0)
            {
                packed = IntPtr.Zero;
                scaleX = 1f;
                scaleY = 1f;
                return 0;
            }
            packed = snapshot.packed;
            scaleX = BallsNative.balls_render_pack_scale(snapshot.fieldWidth);
            scaleY = BallsNative.balls_render_pack_scale(snapshot.fieldHeight);
            return (int)System.Math.Min(snapshot.count, int.MaxValue);
        }

        public override void Dispose()
        {
            if (session != IntPtr.Zero)
            {
                BallsNative.balls_session_destroy(session);
            }
        }

        private void RefreshFrame()
        {
            BallsNative.balls_session_board_info(session, out var info);
            frame = new BoardFrame
            {
                columns = info.columns,
                rows = info.rows,
                playableHeight = info.playableHeight,
                paddedWidth = info.paddedWidth,
                paddedCellCount = info.paddedCellCount,
                brickCount = info.brickCount,
                aliveBrickCount = info.aliveBrickCount,
                revision = (uint)info.revision,
                launcherColumns = info.launcherColumns,
                launcherVisibleRows = info.launcherVisibleRows,
                launcherRemainingBalls = info.launcherRemainingBalls,
            };
        }

        private static byte[] ReadArray(IntPtr pointer, int count, int stride)
        {
            var bytes = new byte[count * stride];
            if (pointer != IntPtr.Zero && count > 0)
            {
                Marshal.Copy(pointer, bytes, 0, bytes.Length);
            }
            return bytes;
        }
    }
}
