using System;
using System.Collections.Generic;
using Unity.Collections;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

    public struct BrickCell
    {
        public int column;
        public int row;

        public BrickCell(int column, int row)
        {
            this.column = column;
            this.row = row;
        }
    }

    public sealed class Brick
    {
        public int sourceId = LevelDocument.EmptyBrickId;
        public int color = 0;
        public int maxHits = 1;
        public int hitsLeft = 1;
        public List<BrickCell> cells = new();
        public float riseOffset = 0.0f;
        public float hitFlash = 0.0f;

        public bool IsAlive => hitsLeft > 0;
    }

    public sealed class BurstBoard : IDisposable
    {
        public const float RiseSpeedCellsPerSecond = 10.0f;
        public const float HitFlashSeconds = 0.15f;
        public const int EmptyCell = -1;
        public const int BallColorCount = 7;
        public const int NoTarget = -1;
        public const int PaddingCell = -2;

        private int columnCount;
        private int rowCount;
        private NativeArray<int> cells;
        private NativeArray<int> colors;
        private NativeArray<int> nearFlags;
        private NativeArray<int> targets;

        private readonly List<Brick> brickList = new();
        private int[] nearCounts = Array.Empty<int>();
        private readonly List<int> dirtyCells = new();
        private byte[] cellChanged = Array.Empty<byte>();

        private readonly ulong[] remainingHits = new ulong[BallColorCount];
        private int aliveCount;
        private readonly List<int> aliveBricks = new();
        private readonly List<int> alivePosition = new();
        private readonly List<int> animatingBricks = new();
        private byte[] brickAnimating = Array.Empty<byte>();

        private struct SettleItem : IComparable<SettleItem>
        {
            public int row;
            public int brickIndex;

            public SettleItem(int row, int brickIndex)
            {
                this.row = row;
                this.brickIndex = brickIndex;
            }

            public int CompareTo(SettleItem other)
            {
                int c = row.CompareTo(other.row);
                return c != 0 ? c : brickIndex.CompareTo(other.brickIndex);
            }
        }

        private readonly List<SettleItem> bricksToSettle = new();
        private bool wholeBoardSettlePending;
        private uint changeRevision = 0;

        public int Columns => columnCount;
        public int Rows => rowCount;
        public int PlayableHeight => rowCount;
        public int PaddedWidth => columnCount + 2;
        public int PaddedHeight => rowCount + 2;
        public int PaddedCellCount => PaddedWidth * PaddedHeight;
        public int BrickCount => brickList.Count;
        public int AliveBrickCount => aliveCount;
        public uint Revision => changeRevision;
        public IReadOnlyList<Brick> Bricks => brickList;

        public int PaddedIndex(int column, int row) => (row + 1) * PaddedWidth + (column + 1);

        public int Cell(int index) => cells[index];
        public int BrickAt(int column, int row) => cells[PaddedIndex(column, row)];
        public int BrickColor(int brickIndex) => brickList[brickIndex].color;
        public int BrickHitsLeft(int brickIndex) => brickList[brickIndex].hitsLeft;
        public float BrickRiseOffset(int brickIndex) => brickList[brickIndex].riseOffset;
        public float BrickHitFlash(int brickIndex) => brickList[brickIndex].hitFlash;
        public ulong RemainingHitsByColor(int color) => color >= 0 && color < BallColorCount ? remainingHits[color] : 0;

        public BoardView BoardView => new BoardView(cells, colors, targets, nearFlags);

        public BurstBoard()
        {
        }

        public void Load(BrickLayout layout)
        {
            columnCount = layout.width;
            rowCount = layout.playableHeight;

            int totalPadded = PaddedCellCount;
            ReallocateArrays(totalPadded, layout.types.Count);

            for (int i = 0; i < totalPadded; ++i)
            {
                cells[i] = EmptyCell;
            }

            brickList.Clear();
            Array.Clear(remainingHits, 0, remainingHits.Length);

            var brickIndexById = new Dictionary<int, int>();
            var typesById = layout.TypesById();

            for (int row = 0; row < layout.height; ++row)
            {
                for (int col = 0; col < layout.width; ++col)
                {
                    int id = layout.CellId(col, row);
                    if (id == BrickLayout.EmptyBrickId || !typesById.TryGetValue(id, out var type))
                    {
                        continue;
                    }

                    if (!brickIndexById.TryGetValue(id, out int bIndex))
                    {
                        bIndex = brickList.Count;
                        brickIndexById[id] = bIndex;
                        var newBrick = new Brick
                        {
                            sourceId = id,
                            color = type.color,
                            maxHits = type.hits,
                            hitsLeft = type.hits,
                            riseOffset = 0.0f,
                            hitFlash = 0.0f
                        };
                        brickList.Add(newBrick);
                        colors[bIndex] = type.color;
                        remainingHits[type.color] += (ulong)type.hits;
                    }

                    brickList[bIndex].cells.Add(new BrickCell(col, row));
                    cells[PaddedIndex(col, row)] = bIndex;
                }
            }

            aliveCount = brickList.Count;
            aliveBricks.Clear();
            alivePosition.Clear();
            for (int index = 0; index < aliveCount; ++index)
            {
                aliveBricks.Add(index);
                alivePosition.Add(index);
            }

            animatingBricks.Clear();
            if (brickAnimating.Length < brickList.Count)
            {
                brickAnimating = new byte[brickList.Count];
            }
            else
            {
                Array.Clear(brickAnimating, 0, brickAnimating.Length);
            }

            bricksToSettle.Clear();
            wholeBoardSettlePending = true;

            RebuildNearCounts();
            Array.Clear(cellChanged, 0, cellChanged.Length);
            dirtyCells.Clear();

            ++changeRevision;

            RebuildTargetField();
        }

        private void ReallocateArrays(int paddedCount, int maxBricks)
        {
            if (cells.IsCreated && cells.Length != paddedCount)
            {
                cells.Dispose();
                nearFlags.Dispose();
                targets.Dispose();
            }

            if (!cells.IsCreated)
            {
                cells = new NativeArray<int>(paddedCount, Allocator.Persistent);
                nearFlags = new NativeArray<int>(paddedCount, Allocator.Persistent);
                targets = new NativeArray<int>(paddedCount * BallColorCount, Allocator.Persistent);
                nearCounts = new int[paddedCount];
                cellChanged = new byte[paddedCount];
            }

            int neededBricks = math.max(16, maxBricks);
            if (colors.IsCreated && colors.Length < neededBricks)
            {
                colors.Dispose();
            }
            if (!colors.IsCreated)
            {
                colors = new NativeArray<int>(neededBricks, Allocator.Persistent);
            }
        }

        private void RebuildNearCounts()
        {
            Array.Clear(nearCounts, 0, nearCounts.Length);
            for (int i = 0; i < nearFlags.Length; ++i)
            {
                nearFlags[i] = 0;
            }

            int width = PaddedWidth;
            int height = rowCount + 2;

            for (int row = 1; row < height - 1; ++row)
            {
                for (int col = 1; col < width - 1; ++col)
                {
                    if (cells[row * width + col] == EmptyCell)
                    {
                        continue;
                    }
                    for (int nr = row - 1; nr <= row + 1; ++nr)
                    {
                        for (int nc = col - 1; nc <= col + 1; ++nc)
                        {
                            int near = nr * width + nc;
                            nearCounts[near]++;
                            nearFlags[near] = 1;
                        }
                    }
                }
            }
        }

        public void ClearChangedCells()
        {
            for (int i = 0; i < dirtyCells.Count; ++i)
            {
                cellChanged[dirtyCells[i]] = 0;
            }
            dirtyCells.Clear();
        }

        private void MarkCellChanged(int index)
        {
            if (cellChanged[index] == 0)
            {
                cellChanged[index] = 1;
                dirtyCells.Add(index);
            }
        }

        private void SetCell(int index, int value)
        {
            int previous = cells[index];
            if (previous == value)
            {
                return;
            }
            cells[index] = value;
            MarkCellChanged(index);

            if ((previous == EmptyCell) == (value == EmptyCell))
            {
                return;
            }

            int delta = value == EmptyCell ? -1 : 1;
            int width = PaddedWidth;
            int[] nearOffsets = {
                -width - 1, -width, -width + 1,
                -1, 0, 1,
                width - 1, width, width + 1
            };

            for (int i = 0; i < nearOffsets.Length; ++i)
            {
                int near = index + nearOffsets[i];
                int count = nearCounts[near] + delta;
                nearCounts[near] = count;
                int flag = count > 0 ? 1 : 0;
                if (nearFlags[near] != flag)
                {
                    nearFlags[near] = flag;
                    MarkCellChanged(near);
                }
            }
        }

        private void PlaceBrick(int brickIndex, int value)
        {
            var brickCells = brickList[brickIndex].cells;
            for (int i = 0; i < brickCells.Count; ++i)
            {
                SetCell(PaddedIndex(brickCells[i].column, brickCells[i].row), value);
            }
        }

        public bool ApplyHit(int brickIndex)
        {
            if (brickIndex < 0 || brickIndex >= brickList.Count) return false;
            Brick brick = brickList[brickIndex];
            if (!brick.IsAlive)
            {
                return false;
            }

            --brick.hitsLeft;
            --remainingHits[brick.color];
            brick.hitFlash = HitFlashSeconds;
            StartAnimating(brickIndex);

            if (brick.IsAlive)
            {
                return true;
            }

            PlaceBrick(brickIndex, EmptyCell);
            QueueBricksBelow(brickIndex);
            --aliveCount;
            MarkDead(brickIndex);
            ++changeRevision;
            return true;
        }

        public bool ApplyHitsDownTo(int brickIndex, int targetHitsLeft)
        {
            if (brickIndex < 0 || brickIndex >= brickList.Count) return false;
            bool changed = false;
            while (brickList[brickIndex].hitsLeft > math.max(targetHitsLeft, 0))
            {
                changed = ApplyHit(brickIndex) || changed;
            }
            return changed;
        }

        private int FreeRowsAbove(int brickIndex)
        {
            int distance = rowCount;
            var bCells = brickList[brickIndex].cells;
            for (int i = 0; i < bCells.Count; ++i)
            {
                var cell = bCells[i];
                int free = 0;
                for (int row = cell.row - 1; row >= 0 && free < distance; --row)
                {
                    int above = BrickAt(cell.column, row);
                    if (above == brickIndex)
                    {
                        free = distance;
                        break;
                    }
                    if (above != EmptyCell)
                    {
                        break;
                    }
                    ++free;
                }
                distance = math.min(distance, free);
                if (distance == 0)
                {
                    return 0;
                }
            }
            return distance;
        }

        private bool RaiseFully(int brickIndex)
        {
            int distance = FreeRowsAbove(brickIndex);
            if (distance == 0)
            {
                return false;
            }
            Brick brick = brickList[brickIndex];
            PlaceBrick(brickIndex, EmptyCell);
            for (int i = 0; i < brick.cells.Count; ++i)
            {
                var c = brick.cells[i];
                c.row -= distance;
                brick.cells[i] = c;
            }
            PlaceBrick(brickIndex, brickIndex);
            brick.riseOffset += (float)distance;
            StartAnimating(brickIndex);
            return true;
        }

        private int LowestRowInColumn(int brickIndex, int column)
        {
            int lowest = -1;
            var bCells = brickList[brickIndex].cells;
            for (int i = 0; i < bCells.Count; ++i)
            {
                if (bCells[i].column == column)
                {
                    lowest = math.max(lowest, bCells[i].row);
                }
            }
            return lowest;
        }

        private void QueueFirstBrickBelow(int column, int row)
        {
            for (int below = row + 1; below < rowCount; ++below)
            {
                int brick = BrickAt(column, below);
                if (brick != EmptyCell)
                {
                    HeapPush(new SettleItem(below, brick));
                    return;
                }
            }
        }

        private void QueueBricksBelow(int brickIndex)
        {
            var bCells = brickList[brickIndex].cells;
            for (int i = 0; i < bCells.Count; ++i)
            {
                if (LowestRowInColumn(brickIndex, bCells[i].column) == bCells[i].row)
                {
                    QueueFirstBrickBelow(bCells[i].column, bCells[i].row);
                }
            }
        }

        private void HeapPush(SettleItem item)
        {
            bricksToSettle.Add(item);
            int i = bricksToSettle.Count - 1;
            while (i > 0)
            {
                int parent = (i - 1) / 2;
                if (bricksToSettle[i].CompareTo(bricksToSettle[parent]) >= 0) break;
                SettleItem tmp = bricksToSettle[i];
                bricksToSettle[i] = bricksToSettle[parent];
                bricksToSettle[parent] = tmp;
                i = parent;
            }
        }

        private SettleItem HeapPop()
        {
            SettleItem min = bricksToSettle[0];
            int last = bricksToSettle.Count - 1;
            bricksToSettle[0] = bricksToSettle[last];
            bricksToSettle.RemoveAt(last);
            int count = bricksToSettle.Count;
            int i = 0;
            while (true)
            {
                int left = 2 * i + 1;
                int right = 2 * i + 2;
                int smallest = i;
                if (left < count && bricksToSettle[left].CompareTo(bricksToSettle[smallest]) < 0) smallest = left;
                if (right < count && bricksToSettle[right].CompareTo(bricksToSettle[smallest]) < 0) smallest = right;
                if (smallest == i) break;
                SettleItem tmp = bricksToSettle[i];
                bricksToSettle[i] = bricksToSettle[smallest];
                bricksToSettle[smallest] = tmp;
                i = smallest;
            }
            return min;
        }

        private bool SettleWholeBoard()
        {
            var order = new List<int>(brickList.Count);
            for (int index = 0; index < brickList.Count; ++index)
            {
                if (brickList[index].IsAlive)
                {
                    order.Add(index);
                }
            }

            order.Sort((a, b) =>
            {
                int topA = int.MaxValue;
                for (int i = 0; i < brickList[a].cells.Count; ++i) topA = math.min(topA, brickList[a].cells[i].row);
                int topB = int.MaxValue;
                for (int i = 0; i < brickList[b].cells.Count; ++i) topB = math.min(topB, brickList[b].cells[i].row);
                return topA.CompareTo(topB);
            });

            bool anyMoved = false;
            for (int i = 0; i < order.Count; ++i)
            {
                int brick = order[i];
                if (RaiseFully(brick))
                {
                    anyMoved = true;
                    QueueBricksBelow(brick);
                }
            }
            return anyMoved;
        }

        public bool SettleColumns()
        {
            bool anyMoved = false;
            if (wholeBoardSettlePending)
            {
                wholeBoardSettlePending = false;
                anyMoved = SettleWholeBoard();
            }

            while (bricksToSettle.Count > 0)
            {
                int brick = HeapPop().brickIndex;
                if (brickList[brick].IsAlive && RaiseFully(brick))
                {
                    anyMoved = true;
                    QueueBricksBelow(brick);
                }
            }

            if (anyMoved)
            {
                ++changeRevision;
                RebuildTargetField();
            }
            return anyMoved;
        }

        public void Animate(float deltaTime)
        {
            int kept = 0;
            for (int i = 0; i < animatingBricks.Count; ++i)
            {
                int index = animatingBricks[i];
                Brick brick = brickList[index];
                brick.riseOffset = math.max(0.0f, brick.riseOffset - RiseSpeedCellsPerSecond * deltaTime);
                brick.hitFlash = math.max(0.0f, brick.hitFlash - deltaTime);
                if (brick.riseOffset > 0.0f || brick.hitFlash > 0.0f)
                {
                    animatingBricks[kept++] = index;
                }
                else
                {
                    brickAnimating[index] = 0;
                }
            }
            if (kept < animatingBricks.Count)
            {
                animatingBricks.RemoveRange(kept, animatingBricks.Count - kept);
            }
        }

        private void StartAnimating(int brickIndex)
        {
            if (brickAnimating[brickIndex] == 0)
            {
                brickAnimating[brickIndex] = 1;
                animatingBricks.Add(brickIndex);
            }
        }

        private void MarkDead(int brickIndex)
        {
            int position = alivePosition[brickIndex];
            int last = aliveBricks[aliveBricks.Count - 1];
            aliveBricks[position] = last;
            alivePosition[last] = position;
            aliveBricks.RemoveAt(aliveBricks.Count - 1);
        }

        public void RebuildTargetField()
        {
            if (!targets.IsCreated) return;

            int paddedW = PaddedWidth;
            int paddedRows = rowCount + 2;
            int cellCount = paddedW * paddedRows;

            var frontier = new List<int>(cellCount);

            int[] neighborOffsets = {
                -1, 1, -paddedW, paddedW,
                -paddedW - 1, -paddedW + 1, paddedW - 1, paddedW + 1
            };

            for (int colorIndex = 0; colorIndex < BallColorCount; ++colorIndex)
            {
                int colorOffset = colorIndex * cellCount;

                for (int i = 0; i < cellCount; ++i)
                {
                    targets[colorOffset + i] = NoTarget;
                }

                for (int col = 0; col < paddedW; ++col)
                {
                    targets[colorOffset + col] = PaddingCell;
                    targets[colorOffset + (paddedRows - 1) * paddedW + col] = PaddingCell;
                }
                for (int row = 0; row < paddedRows; ++row)
                {
                    targets[colorOffset + row * paddedW] = PaddingCell;
                    targets[colorOffset + row * paddedW + (paddedW - 1)] = PaddingCell;
                }

                frontier.Clear();
                for (int row = 1; row <= rowCount; ++row)
                {
                    for (int col = 1; col <= columnCount; ++col)
                    {
                        int index = row * paddedW + col;
                        int brick = cells[index];
                        if (brick >= 0 && colors[brick] == colorIndex)
                        {
                            targets[colorOffset + index] = index;
                            frontier.Add(index);
                        }
                    }
                }

                for (int head = 0; head < frontier.Count; ++head)
                {
                    int index = frontier[head];
                    int targetVal = targets[colorOffset + index];
                    for (int n = 0; n < neighborOffsets.Length; ++n)
                    {
                        int neighbor = index + neighborOffsets[n];
                        if (targets[colorOffset + neighbor] == NoTarget)
                        {
                            targets[colorOffset + neighbor] = targetVal;
                            frontier.Add(neighbor);
                        }
                    }
                }

                for (int row = 0; row < paddedRows; ++row)
                {
                    for (int col = 0; col < paddedW; ++col)
                    {
                        int index = row * paddedW + col;
                        if (targets[colorOffset + index] != PaddingCell)
                        {
                            continue;
                        }
                        if (frontier.Count == 0)
                        {
                            targets[colorOffset + index] = NoTarget;
                            continue;
                        }
                        int clampedCol = col < 1 ? 1 : (col > columnCount ? columnCount : col);
                        int clampedRow = row < 1 ? 1 : (row > rowCount ? rowCount : row);
                        targets[colorOffset + index] = targets[colorOffset + clampedRow * paddedW + clampedCol];
                    }
                }
            }
        }

        public void Dispose()
        {
            if (cells.IsCreated) cells.Dispose();
            if (colors.IsCreated) colors.Dispose();
            if (nearFlags.IsCreated) nearFlags.Dispose();
            if (targets.IsCreated) targets.Dispose();
        }
    }
}
