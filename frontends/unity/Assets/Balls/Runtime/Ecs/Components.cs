using System;
using Unity.Entities;

namespace Balls.Ecs
{
    /// <summary>Board geometry for the level being played, written by the session system.</summary>
    public struct BoardInfoData : IComponentData
    {
        public int columns;
        public int rows;
        public int playableHeight;
        public int paddedWidth;
        public float scaleX;
        public float scaleY;
        public uint revision;
        public bool valid;
        public int launcherColumns;
        public int launcherVisibleRows;
        public int aliveBricks;
        public int brickCount;
    }

    /// <summary>Everything the debug gadget reports, refreshed by the session system each frame.</summary>
    public struct SessionStatsData : IComponentData
    {
        public ulong ballCount;
        public ulong liveBalls;
        public ulong spawned;
        public ulong hits;
        public double physicsMs;
        public int ticks;
        public ulong color0, color1, color2, color3, color4, color5, color6;
        public ulong waitingToSpawn;
        public bool collisions;
        public double frameMs;
        public double frameMinFps;
        public float fps;

        public ulong ByColor(int color) => color switch
        {
            0 => color0,
            1 => color1,
            2 => color2,
            3 => color3,
            4 => color4,
            5 => color5,
            6 => color6,
            _ => 0,
        };
    }

    /// <summary>
    /// One padded board cell, in row-major order. Cells rather than bricks, because a brick can span several cells
    /// and the board is drawn as a cell grid; the border ring is padding and carries brick -1.
    /// </summary>
    public struct CellElement : IBufferElementData
    {
        public int brick;
        public int color;
        public int hitsLeft;
        public float rise;
        public float flash;
    }

    /// <summary>Launcher slots of the queue, in reading order: row-major, <see cref="BoardInfoData"/>-less.</summary>
    public struct LauncherElement : IBufferElementData
    {
        public int column;
        public int row;
        public int color;
        public int ballCount;
        public float rise;
        public bool present;
    }

    /// <summary>
    /// The live balls as the physics backend packed them: bits 0-13 x, 14-27 y, 28-31 colour. The pointer is
    /// core-owned and only meaningful until the next session update, so the renderer consumes it in the same frame.
    /// </summary>
    public struct BallBatch : IComponentData
    {
        public IntPtr packed;
        public int count;
        public float scaleX;
        public float scaleY;
    }

    public struct SessionInfo : IComponentData
    {
        public int mode;
        public int state;
        public int backend;
        public bool nativeBackend;
        public double elapsed;
        public int launcherRemaining;
    }
}
