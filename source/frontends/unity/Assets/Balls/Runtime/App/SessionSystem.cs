using System.Collections.Generic;
using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using UnityEngine;
using Unity.Entities;

namespace Balls.App
{

    public partial class SessionSystem : SystemBase
    {
        private GameBackend backend;
        private Entity boardEntity;
        private BoardFrame frame;
        private readonly List<CellElement> cells = new();
        private System.IntPtr sessionHandle;

        private System.IntPtr frameTimings;
        private double m_physicsFps;
        private double m_physicsTimer;
        private int m_physicsTicks;
        private readonly List<LauncherElement> launchers = new();

        public static SessionSystem Current { get; private set; }

        public bool IsRunning => backend != null;

        public string LoadFailure { get; set; }

        public System.IntPtr FrameTimingsHandle => frameTimings; public System.IntPtr SessionHandle => sessionHandle;

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

            float frameSeconds = SystemAPI.Time.DeltaTime;
            if (frameSeconds > 0f)
            {
                double frameMs = frameSeconds * 1000.0;
                BallsNative.balls_frame_timings_record(frameTimings, frameMs, frameMs, 0.0);
            }
            if (backend == null)
            {
                return;
            }

            bool paused = BallsApp.Current != null && BallsApp.Current.IsPaused;

            if (!paused)
            {
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
            }

            if (backend.TakeCollisionsChange() is int change && change != 0)
            {
                Notice = BallsNative.BallCollisionsMessage(change);
            }

            if (backend.Frame.revision != frame.revision || backend.Frame.paddedCellCount != frame.paddedCellCount)
            {
                RefreshBoard();
            }
            ReadLaunchers();
            Publish(paused ? 0f : SystemAPI.Time.DeltaTime);
        }

        public bool StartSession(in BallsInput.StartRequest request, out string error)
        {
            backend?.Dispose();
            backend = null;
            sessionHandle = System.IntPtr.Zero;
            error = null;
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
            catch (System.Exception ex)
            {
                LoadFailure = ex.Message;
                error = ex.Message;
                EntityManager.SetComponentData(boardEntity, new BoardInfoData());
                return false;
            }
            LoadFailure = null;
            backend.SetVisibilityGrid(160, 90);
            ApplyBallCollisions(request.collisions);
            frame = backend.Frame;
            RefreshBoard();
            ReadLaunchers();
            Publish(0f);
            return true;
        }

        public void RestartSession()
        {
            if (backend != null)
            {
                backend.Restart();
                RefreshBoard();
            }
        }

        public void EndSession()
        {
            backend?.Dispose();
            backend = null;
            sessionHandle = System.IntPtr.Zero;
            LoadFailure = null;
            EntityManager.SetComponentData(boardEntity, new BoardInfoData());
            EntityManager.SetComponentData(boardEntity, new BallBatch());
            EntityManager.SetComponentData(boardEntity, new SessionInfo());
            EntityManager.SetComponentData(boardEntity, new SessionStatsData());
            EntityManager.GetBuffer<CellElement>(boardEntity).Clear();
            EntityManager.GetBuffer<LauncherElement>(boardEntity).Clear();
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

            BallsNative.balls_frame_timings_stats(frameTimings, out NativeFrameStats frames);

            var stats = backend.Stats;

            m_physicsTimer += deltaSeconds;
            m_physicsTicks += stats.ticks;
            if (m_physicsTimer >= 0.25)
            {
                m_physicsFps = m_physicsTicks / m_physicsTimer;
                m_physicsTicks = 0;
                m_physicsTimer = 0.0;
            }

            EntityManager.SetComponentData(boardEntity, new SessionStatsData
            {
                ballCount = stats.ballCount,
                liveBalls = stats.liveBallCount,
                spawned = stats.spawned,
                hits = stats.hits,
                physicsMs = stats.physicsMs,
                physicsFps = backend.PhysicsThreadFps >= 0.0 ? backend.PhysicsThreadFps : m_physicsFps,
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
