using System.Collections.Generic;
using Balls.Ecs;
using Balls.Game;
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
        private readonly List<LauncherElement> launchers = new();

        public bool IsRunning => backend != null;

        public GameBackend Backend => backend;

        protected override void OnCreate()
        {
            boardEntity = EntityManager.CreateEntity();
            EntityManager.AddComponentData(boardEntity, new BoardInfoData());
            EntityManager.AddComponentData(boardEntity, new BallBatch());
            EntityManager.AddComponentData(boardEntity, new SessionInfo());
            EntityManager.AddBuffer<CellElement>(boardEntity);
            EntityManager.AddBuffer<LauncherElement>(boardEntity);
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
            backend = new NativeGameBackend(request.mode, request.levelDirectory, request.levelName, request.backend);
            // The GPU backends sample one grid cell per ball-sized block of the render target; a 1920-wide frame at
            // the 0.4-cell ball diameter is the default the other front ends use.
            backend.SetVisibilityGrid(160, 90);
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
                nativeBackend = backend.IsNative,
                elapsed = backend.Elapsed,
                launcherRemaining = frame.launcherRemainingBalls,
            });
        }
    }
}
