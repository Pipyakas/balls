using System;
using System.Collections.Generic;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{
    /// <summary>
    /// A launcher slot stored in the active queue with per-slot animation state.
    /// Mirrored from source/core/game/LauncherQueue.h:9-12.
    /// </summary>
    public sealed class QueuedLauncher
    {
        public Launcher launcher;
        public float riseOffset;

        public QueuedLauncher(Launcher launcher, float riseOffset = 0.0f)
        {
            this.launcher = launcher;
            this.riseOffset = riseOffset;
        }
    }

    /// <summary>
    /// Column-based launcher queue and rotation animation.
    /// Mirrored from source/core/game/LauncherQueue.h / LauncherQueue.cpp (lines 11-85).
    /// </summary>
    public sealed class BurstLauncherQueue
    {
        public const float RiseSpeedRowsPerSecond = 8.0f;

        private readonly List<List<QueuedLauncher>> columns = new();
        private int visibleRowCount;
        private int rotationColumn;

        public int ColumnCount => columns.Count;
        public int VisibleRows => visibleRowCount;

        /// <summary>
        /// Populates the launcher queue columns from a LauncherLayout.
        /// Mirrored from source/core/game/LauncherQueue.cpp:11-23.
        /// </summary>
        public void Load(LauncherLayout layout)
        {
            columns.Clear();
            for (int col = 0; col < layout.columns; ++col)
            {
                columns.Add(new List<QueuedLauncher>());
            }
            visibleRowCount = layout.visibleRows;
            rotationColumn = 0;

            int rowCount = layout.RowCount;
            for (int row = 0; row < rowCount; ++row)
            {
                for (int col = 0; col < layout.columns; ++col)
                {
                    var slot = layout.Slot(col, row);
                    if (slot.HasValue)
                    {
                        columns[col].Add(new QueuedLauncher(slot.Value, 0.0f));
                    }
                }
            }
        }

        /// <summary>
        /// Advances rise offset animation for all queued launchers.
        /// Mirrored from source/core/game/LauncherQueue.cpp:25-33.
        /// </summary>
        public void Animate(float deltaTime)
        {
            for (int c = 0; c < columns.Count; ++c)
            {
                var column = columns[c];
                for (int i = 0; i < column.Count; ++i)
                {
                    column[i].riseOffset = math.max(0.0f, column[i].riseOffset - RiseSpeedRowsPerSecond * deltaTime);
                }
            }
        }

        /// <summary>
        /// Takes a launcher from a specific column and visible row.
        /// Mirrored from source/core/game/LauncherQueue.cpp:35-50.
        /// </summary>
        public Launcher? Take(int column, int row)
        {
            if (column < 0 || column >= ColumnCount || row < 0 || row >= visibleRowCount)
            {
                return null;
            }
            var queue = columns[column];
            if (row >= queue.Count)
            {
                return null;
            }

            Launcher taken = queue[row].launcher;
            queue.RemoveAt(row);
            for (int behind = row; behind < queue.Count; ++behind)
            {
                queue[behind].riseOffset += 1.0f;
            }
            return taken;
        }

        /// <summary>
        /// Takes the next available launcher in round-robin column rotation.
        /// Mirrored from source/core/game/LauncherQueue.cpp:52-62.
        /// </summary>
        public Launcher? TakeNextInRotation()
        {
            int colCount = ColumnCount;
            if (colCount == 0) return null;

            for (int attempt = 0; attempt < colCount; ++attempt)
            {
                int col = (rotationColumn + attempt) % colCount;
                if (columns[col].Count > 0)
                {
                    rotationColumn = (col + 1) % colCount;
                    return Take(col, 0);
                }
            }
            return null;
        }

        /// <summary>
        /// Inspects the launcher visible at (column, row).
        /// Mirrored from source/core/game/LauncherQueue.cpp:64-70.
        /// </summary>
        public QueuedLauncher Visible(int column, int row)
        {
            if (column < 0 || column >= ColumnCount || row < 0 || row >= visibleRowCount)
            {
                return null;
            }
            var queue = columns[column];
            return row < queue.Count ? queue[row] : null;
        }

        /// <summary>
        /// Checks if all columns in the launcher queue are empty.
        /// Mirrored from source/core/game/LauncherQueue.cpp:72-74.
        /// </summary>
        public bool IsEmpty()
        {
            for (int c = 0; c < columns.Count; ++c)
            {
                if (columns[c].Count > 0) return false;
            }
            return true;
        }

        /// <summary>
        /// Returns total number of launchers currently in all queue columns.
        /// Mirrored from source/core/game/LauncherQueue.cpp:76-82.
        /// </summary>
        public int LauncherCount()
        {
            int total = 0;
            for (int c = 0; c < columns.Count; ++c)
            {
                total += columns[c].Count;
            }
            return total;
        }

        /// <summary>
        /// Tallies remaining unlaunched balls by color across all columns.
        /// Mirrored from source/core/game/LauncherQueue.cpp:84-93.
        /// </summary>
        public ulong[] RemainingBalls()
        {
            var tally = new ulong[7];
            for (int c = 0; c < columns.Count; ++c)
            {
                var col = columns[c];
                for (int i = 0; i < col.Count; ++i)
                {
                    int color = col[i].launcher.color;
                    if (color >= 0 && color < 7)
                    {
                        tally[color] += (ulong)col[i].launcher.ballCount;
                    }
                }
            }
            return tally;
        }

        /// <summary>
        /// Returns the sum total of all unlaunched balls in the queue.
        /// </summary>
        public int TotalRemainingBalls()
        {
            int sum = 0;
            for (int c = 0; c < columns.Count; ++c)
            {
                var col = columns[c];
                for (int i = 0; i < col.Count; ++i)
                {
                    sum += col[i].launcher.ballCount;
                }
            }
            return sum;
        }
    }
}
