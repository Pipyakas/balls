using System;
using System.Collections.Generic;
using Balls.Ecs;

namespace Balls.Game
{
    public struct BoardFrame
    {
        public int columns;
        public int rows;
        public int playableHeight;
        public int paddedWidth;
        public int paddedCellCount;
        public int brickCount;
        public int aliveBrickCount;
        public uint revision;
        public int launcherColumns;
        public int launcherVisibleRows;
        public int launcherRemainingBalls;
    }

    public struct BackendStats
    {
        public ulong ballCount;
        public ulong liveBallCount;
        public ulong spawned;
        public ulong hits;
        public double physicsMs;
        public int ticks;
        public ulong red, orange, yellow, green, blue, indigo, violet;

        public ulong ByColor(int color) => color switch
        {
            0 => red,
            1 => orange,
            2 => yellow,
            3 => green,
            4 => blue,
            5 => indigo,
            6 => violet,
            _ => 0,
        };
    }

    public abstract class GameBackend : IDisposable
    {
        public abstract bool IsNative { get; }

        public abstract int Mode { get; }

        public abstract int State { get; }

        public abstract int BackendKind { get; }

        public abstract double Elapsed { get; }

        public abstract BoardFrame Frame { get; }

        public abstract BackendStats Stats { get; }

        public abstract void Update(float deltaSeconds);

        public abstract void Restart();

        public abstract bool ChooseLauncher(int column, int row);

        public abstract void StartAutoRelease();

        public abstract void SelectSpawnRate(int index);

        public abstract void SelectBallLimit(int index);

        public abstract bool SwitchBackend(int kind);

        public virtual int PendingBackend => -1;

        public virtual int TakeSwitchOutcome(out int kind)
        {
            kind = -1;
            return 0;
        }

        public abstract void StartProbe(double targetFps);

        public abstract void ReportFrame(double frameSeconds, double workSeconds);

        public abstract void ReadCells(List<CellElement> destination);

        public abstract void ReadLaunchers(List<LauncherElement> destination);

        public abstract int ReadBalls(out IntPtr packed, out float scaleX, out float scaleY);

        public abstract string LastFault { get; }

        public virtual void SetVisibilityGrid(int width, int height)
        {
        }

        public virtual int SpawnRateIndex => 0;
        public virtual int BallLimitIndex => 0;

        public virtual bool EndlessSpawn => false;
        public virtual ulong BallLimit => 0;
        public virtual bool ProbeActive => false;
        public virtual int ProbePhase => 0;
        public virtual double ProbeFps => 0.0;
        public virtual double ProbeTargetFps => 60.0;
        public virtual ulong ProbeBest => 0;
        public virtual bool HasMachineLimit => false;
        public virtual ulong MachineLimit => 0;
        public virtual bool BallCollisions => false;

        public virtual int SetBallCollisions(bool enabled) => 0;

        public virtual int TakeCollisionsChange() => 0;

        public virtual double PhysicsThreadFps => -1.0;
        public virtual void CancelProbe() { }
        public virtual void UseMachineLimit(ulong limit) { }

        public virtual void Dispose()
        {
        }
    }
}
