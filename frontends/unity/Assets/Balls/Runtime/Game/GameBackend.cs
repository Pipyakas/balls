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

    /// <summary>
    /// What the front end needs from a game session, whichever side runs it. The native implementation forwards to
    /// balls_unity.dll and the C++ core; a Burst implementation runs the same rules and the same ball step in jobs.
    /// Systems only ever see this surface, so the two can be swapped from the settings page or compared side by side.
    /// </summary>
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

        public abstract void SelectBallMultiplier(int index);

        public abstract void SelectBallLimit(int index);

        public abstract bool SwitchBackend(int kind);

        /// <summary>The backend being built in the background, or -1. Taking over happens on the session's own thread.</summary>
        public virtual int PendingBackend => -1;

        /// <summary>0 when no request has finished, 1 when it succeeded, 2 when it failed.</summary>
        public virtual int TakeSwitchOutcome(out int kind)
        {
            kind = -1;
            return 0;
        }

        public abstract void StartProbe(double targetFps);

        public abstract void ReportFrame(double frameSeconds, double workSeconds);

        /// <summary>
        /// Fills <paramref name="destination"/> with every padded board cell, row-major; resizes it to
        /// paddedCellCount. The border ring holds brick -1.
        /// </summary>
        public abstract void ReadCells(List<CellElement> destination);

        /// <summary>Fills <paramref name="destination"/> with every launcher slot, present or not.</summary>
        public abstract void ReadLaunchers(List<LauncherElement> destination);

        /// <summary>
        /// Points at the packed ball array for this frame. The memory belongs to the backend and is invalidated by
        /// the next <see cref="Update"/>.
        /// </summary>
        public abstract int ReadBalls(out IntPtr packed, out float scaleX, out float scaleY);

        public abstract string LastFault { get; }

        /// <summary>
        /// How finely the GPU backends sample the board when drawing millions of balls. Native backends use it;
        /// a Burst backend renders from its own arrays and ignores it.
        /// </summary>
        public virtual void SetVisibilityGrid(int width, int height)
        {
        }

        // Performance-mode readouts and controls the HUD needs but the session state does not carry.
        public virtual int SpawnRateIndex => 0;
        public virtual int BallMultiplierIndex => 0;
        public virtual int BallLimitIndex => 0;
        public virtual ulong BallLimit => 0;
        public virtual bool ProbeActive => false;
        public virtual int ProbePhase => 0;
        public virtual double ProbeFps => 0.0;
        public virtual double ProbeTargetFps => 60.0;
        public virtual ulong ProbeBest => 0;
        public virtual bool HasMachineLimit => false;
        public virtual ulong MachineLimit => 0;
        public virtual bool BallCollisions => false;
        /// <summary>
        /// Applies the Ball collisions setting. Returns the core's change (0 none, 1 switching to Scalar, 2 Scalar not
        /// offered) so the caller can show its toast; backends without collisions report no change.
        /// </summary>
        public virtual int SetBallCollisions(bool enabled) => 0;
        public virtual void CancelProbe() { }
        public virtual void UseMachineLimit(ulong limit) { }

        public virtual void Dispose()
        {
        }
    }
}
