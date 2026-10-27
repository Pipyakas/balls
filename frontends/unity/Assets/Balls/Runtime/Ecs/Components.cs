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
