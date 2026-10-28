using System.Collections.Generic;
using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using UnityEngine;
using Unity.Entities;

namespace Balls.App
{
    /// <summary>
    /// Owns the game session and publishes it into ECS components: one board singleton carrying the geometry, two
    /// buffers with the cells and the launcher slots, and a ball batch pointing at the physics backend's packed
    /// words. Everything else in the front end reads these; nothing else talks to the backend.
    /// </summary>
    public partial class SessionSystem : SystemBase
    {
        private GameBackend backend;
        private Entity boardEntity;
        private BoardFrame frame;
        private readonly List<CellElement> cells = new();
        private System.IntPtr sessionHandle;
        // The core's rolling one-second window, the same statistics SDL and Unreal show.
        private System.IntPtr frameTimings;
        private readonly List<LauncherElement> launchers = new();

        /// <summary>The live system, so screens can reach the session without a managed component.</summary>
        public static SessionSystem Current { get; private set; }

        public bool IsRunning => backend != null;

        /// <summary>Why the last start request failed (an unreadable level), or null.</summary>
        public string LoadFailure { get; set; }

        /// <summary>A toast the session has for the player (the core's ball collisions notice), taken by BallsApp.</summary>
        public string Notice { get; set; }

        public GameBackend Backend => backend;

        protected override void OnCreate()
        {
            Current = this;
            frameTimings = BallsNative.balls_frame_timings_create();
            boardEntity = EntityManager.CreateEntity();
            EntityManager.AddComponentData(boardEntity, new BoardInfoData());
            EntityManager.AddComponentData(boardEntity, new BallBatch());
            EntityManager.AddComponentData(boardEntity, new SessionInfo());
            EntityManager.AddComponentData(boardEntity, new SessionStatsData());
            EntityManager.AddBuffer<CellElement>(boardEntity);
            EntityManager.AddBuffer<LauncherElement>(boardEntity);
        }

        protected override void OnDestroy()
        {
            if (Current == this)
            {
                Current = null;
            }
            backend?.Dispose();
            backend = null;
            BallsNative.balls_frame_timings_destroy(frameTimings);
            frameTimings = System.IntPtr.Zero;
        }

        protected override void OnUpdate()
        {
            if (backend == null)
            {
                if (!BallsInput.TryTakeStartRequest(out var request))
                {
                    return;
                }
                StartSession(request);
                return;
            }

            if (BallsInput.TryTakeRestart())
            {
                backend.Restart();
                RefreshBoard();
            }
            if (BallsInput.TryTakeLauncher(out int column, out int row))
            {
                backend.ChooseLauncher(column, row);
            }
            if (BallsInput.TryTakeBackendSwitch(out int backendKind))
            {
                backend.SwitchBackend(backendKind);
            }
            if (BallsInput.TryTakeCollisions(out bool collisions))
            {
                ApplyBallCollisions(collisions);
            }
            if (BallsInput.TryTakeAutoRelease())
            {
                backend.StartAutoRelease();
            }

            float deltaSeconds = SystemAPI.Time.DeltaTime;
            backend.Update(deltaSeconds);
            backend.ReportFrame(deltaSeconds, backend.Stats.physicsMs / 1000.0);

            if (backend.Frame.revision != frame.revision || backend.Frame.paddedCellCount != frame.paddedCellCount)
            {
                RefreshBoard();
            }
            ReadLaunchers();
            Publish(deltaSeconds);
        }

        private void StartSession(in BallsInput.StartRequest request)
        {
            backend?.Dispose();
            backend = null;
            sessionHandle = System.IntPtr.Zero;
            try
            {
                if (request.backend == Balls.Physics.Burst.BurstGameBackend.BackendBurst)
                {
                    backend = new Balls.Physics.Burst.BurstGameBackend(request.mode, request.levelDirectory, request.levelName);
                }
                else
                {
                    var created = new NativeGameBackend(request.mode, request.levelDirectory, request.levelName, request.backend);
                    backend = created;
                    sessionHandle = created.SessionHandle;
                }
            }
            catch (System.Exception error)
            {
                LoadFailure = error.Message;
                EntityManager.SetComponentData(boardEntity, new BoardInfoData());
                return;
            }
            // The GPU backends sample one grid cell per ball-sized block of the render target; a 1920-wide frame at
            // the 0.4-cell ball diameter is the default the other front ends use.
            backend.SetVisibilityGrid(160, 90);
            ApplyBallCollisions(request.collisions);
            frame = backend.Frame;
            RefreshBoard();
            ReadLaunchers();
            Publish(0f);
        }

        private void ApplyBallCollisions(bool enabled)
        {
            int change = backend.SetBallCollisions(enabled);
            if (change != 0)
            {
                Notice = BallsNative.BallCollisionsMessage(change);
            }
        }

        private void RefreshBoard()
        {
            frame = backend.Frame;
            backend.ReadCells(cells);
            var buffer = EntityManager.GetBuffer<CellElement>(boardEntity);
            buffer.Clear();
            for (int index = 0; index < cells.Count; ++index)
            {
                buffer.Add(cells[index]);
            }

            backend.ReadLaunchers(launchers);
            var launcherBuffer = EntityManager.GetBuffer<LauncherElement>(boardEntity);
            launcherBuffer.Clear();
            for (int index = 0; index < launchers.Count; ++index)
            {
                launcherBuffer.Add(launchers[index]);
            }

            EntityManager.SetComponentData(boardEntity, new BoardInfoData
            {
                columns = frame.columns,
                rows = frame.rows,
                playableHeight = frame.playableHeight,
                paddedWidth = frame.paddedWidth,
                scaleX = Mathf.Max(1, frame.columns),
                scaleY = Mathf.Max(1, frame.rows),
                revision = frame.revision,
                valid = true,
                launcherColumns = frame.launcherColumns,
                launcherVisibleRows = frame.launcherVisibleRows,
                aliveBricks = frame.aliveBrickCount,
                brickCount = frame.brickCount,
            });
        }

        private void ReadLaunchers()
        {
            backend.ReadLaunchers(launchers);
            var buffer = EntityManager.GetBuffer<LauncherElement>(boardEntity);
            for (int index = 0; index < launchers.Count && index < buffer.Length; ++index)
            {
                buffer[index] = launchers[index];
            }
        }

        private void Publish(float deltaSeconds)
        {
            int ballCount = backend.ReadBalls(out System.IntPtr packed, out float scaleX, out float scaleY);
            EntityManager.SetComponentData(boardEntity, new BallBatch
            {
                packed = packed,
                count = ballCount,
                scaleX = scaleX,
                scaleY = scaleY,
            });
            EntityManager.SetComponentData(boardEntity, new SessionInfo
            {
                mode = backend.Mode,
                state = backend.State,
                backend = backend.BackendKind,
                pendingBackend = backend.PendingBackend,
                nativeBackend = backend.IsNative,
                elapsed = backend.Elapsed,
                launcherRemaining = frame.launcherRemainingBalls,
            });

            if (deltaSeconds > 0f)
            {
                double frameMs = deltaSeconds * 1000.0;
                BallsNative.balls_frame_timings_record(frameTimings, frameMs, frameMs, 0.0);
            }
            BallsNative.balls_frame_timings_stats(frameTimings, out NativeFrameStats frames);

            var stats = backend.Stats;
            EntityManager.SetComponentData(boardEntity, new SessionStatsData
            {
                ballCount = stats.ballCount,
                liveBalls = stats.liveBallCount,
                spawned = stats.spawned,
                hits = stats.hits,
                physicsMs = stats.physicsMs,
                ticks = stats.ticks,
                color0 = stats.red,
                color1 = stats.orange,
                color2 = stats.yellow,
                color3 = stats.green,
                color4 = stats.blue,
                color5 = stats.indigo,
                color6 = stats.violet,
                waitingToSpawn = stats.liveBallCount > stats.ballCount ? stats.liveBallCount - stats.ballCount : 0,
                collisions = backend.BallCollisions,
                frameMs = deltaSeconds * 1000.0,
                frameMinFps = frames.worstFpsLastSecond,
                fps = (float)frames.framesPerSecond,
            });
        }
    }
}
