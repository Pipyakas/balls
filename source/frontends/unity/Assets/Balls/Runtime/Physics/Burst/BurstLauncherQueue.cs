using System;
using System.Collections.Generic;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

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

    public sealed class BurstLauncherQueue
    {
        public const float RiseSpeedRowsPerSecond = 8.0f;

        private readonly List<List<QueuedLauncher>> columns = new();
        private int visibleRowCount;
        private int rotationColumn;

        public int ColumnCount => columns.Count;
        public int VisibleRows => visibleRowCount;

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

        public QueuedLauncher Visible(int column, int row)
        {
            if (column < 0 || column >= ColumnCount || row < 0 || row >= visibleRowCount)
            {
                return null;
            }
            var queue = columns[column];
            return row < queue.Count ? queue[row] : null;
        }

        public bool IsEmpty()
        {
            for (int c = 0; c < columns.Count; ++c)
            {
                if (columns[c].Count > 0) return false;
            }
            return true;
        }

        public int LauncherCount()
        {
            int total = 0;
            for (int c = 0; c < columns.Count; ++c)
            {
                total += columns[c].Count;
            }
            return total;
        }

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
