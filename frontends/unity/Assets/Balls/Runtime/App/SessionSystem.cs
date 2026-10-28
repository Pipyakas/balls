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
        private float m_fps;
        private float m_fpsWindow = float.MaxValue;
        private double m_minFpsTimer;
        private float m_minFps;
        private double m_physicsFps;
        private double m_physicsTimer;
        private int m_physicsTicks;
        private readonly List<LauncherElement> launchers = new();

        /// <summary>The live system, so screens can reach the session without a managed component.</summary>
        public static SessionSystem Current { get; private set; }

        public bool IsRunning => backend != null;

        /// <summary>Why the last start request failed (an unreadable level), or null.</summary>
        public string LoadFailure { get; set; }

        public GameBackend Backend => backend;

        protected override void OnCreate()
        {
            Current = this;
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
                backend.SetBallCollisions(collisions);
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
            backend.SetBallCollisions(request.collisions);
            frame = backend.Frame;
            RefreshBoard();
            ReadLaunchers();
            Publish(0f);
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

            m_fps = m_fps <= 0f ? 1f / Mathf.Max(deltaSeconds, 1e-4f) : Mathf.Lerp(m_fps, 1f / Mathf.Max(deltaSeconds, 1e-4f), 0.1f);
            m_minFpsTimer += deltaSeconds;
            if (m_minFpsTimer >= 1.0)
            {
                m_minFps = m_fpsWindow;
                m_fpsWindow = float.MaxValue;
                m_minFpsTimer = 0f;
            }
            m_fpsWindow = Mathf.Min(m_fpsWindow, 1f / Mathf.Max(deltaSeconds, 1e-4f));

            var stats = backend.Stats;
            // Physics FPS is how many physics frames finished in the last stretch of wall time; the UI rate can differ.
            // ticksLastFrame counts the ticks of one frame, so they are summed over the window.
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
                physicsFps = m_physicsFps,
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
                frameMinFps = m_minFps,
                fps = m_fps,
            });
        }
    }
}
