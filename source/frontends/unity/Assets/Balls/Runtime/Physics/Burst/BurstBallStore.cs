using System;
using System.Runtime.CompilerServices;
using Unity.Burst;
using Unity.Collections;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

    public struct BallData
    {
        public float x;
        public float y;
        public float vx;
        public float vy;
        public int color;
        public int bounces;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public BallData(float x, float y, float vx, float vy, int color, int bounces = 0)
        {
            this.x = x;
            this.y = y;
            this.vx = vx;
            this.vy = vy;
            this.color = color;
            this.bounces = bounces;
        }
    }

    public struct BallSpawn
    {
        public float x;
        public float y;
        public float vx;
        public float vy;
        public int color;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public BallSpawn(float x, float y, float vx, float vy, int color)
        {
            this.x = x;
            this.y = y;
            this.vx = vx;
            this.vy = vy;
            this.color = color;
        }
    }

    public struct BurstBallStore : IDisposable
    {
        private NativeArray<BallData> m_Balls;
        private int m_Count;
        private int m_Capacity;
        private Allocator m_Allocator;

        public float fieldWidth;
        public float fieldHeight;

        public int Count => m_Count;
        public int Capacity => m_Capacity;
        public bool IsCreated => m_Balls.IsCreated;
        public Allocator Allocator => m_Allocator;

        public NativeArray<BallData> Balls => m_Balls;

        public NativeSlice<BallData> ActiveBalls => m_Balls.Slice(0, m_Count);

        public BallData this[int index]
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => m_Balls[index];
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            set => m_Balls[index] = value;
        }

        public BurstBallStore(int initialCapacity, Allocator allocator = Allocator.Persistent)
        {
            m_Allocator = allocator;
            m_Capacity = math.max(16, initialCapacity);
            m_Count = 0;
            m_Balls = new NativeArray<BallData>(m_Capacity, allocator, NativeArrayOptions.UninitializedMemory);
            fieldWidth = 1.0f;
            fieldHeight = 1.0f;
        }

        public void EnsureCapacity(int requiredCapacity)
        {
            if (requiredCapacity <= m_Capacity)
            {
                return;
            }

            int newCapacity = math.max(m_Capacity * 2, requiredCapacity);
            var newArray = new NativeArray<BallData>(newCapacity, m_Allocator, NativeArrayOptions.UninitializedMemory);

            if (m_Balls.IsCreated)
            {
                if (m_Count > 0)
                {
                    NativeArray<BallData>.Copy(m_Balls, newArray, m_Count);
                }
                m_Balls.Dispose();
            }

            m_Balls = newArray;
            m_Capacity = newCapacity;
        }

        public void AddBall(float x, float y, float vx, float vy, int color, int bounces = 0)
        {
            EnsureCapacity(m_Count + 1);
            m_Balls[m_Count++] = new BallData(x, y, vx, vy, color, bounces);
        }

        public void AddBall(BallSpawn spawn)
        {
            AddBall(spawn.x, spawn.y, spawn.vx, spawn.vy, spawn.color, 0);
        }

        public void AddBalls(NativeArray<BallSpawn> spawns)
        {
            if (!spawns.IsCreated || spawns.Length == 0) return;
            EnsureCapacity(m_Count + spawns.Length);
            for (int i = 0; i < spawns.Length; ++i)
            {
                BallSpawn s = spawns[i];
                m_Balls[m_Count++] = new BallData(s.x, s.y, s.vx, s.vy, s.color, 0);
            }
        }

        public void AddBalls(NativeSlice<BallSpawn> spawns)
        {
            if (spawns.Length == 0) return;
            EnsureCapacity(m_Count + spawns.Length);
            for (int i = 0; i < spawns.Length; ++i)
            {
                BallSpawn s = spawns[i];
                m_Balls[m_Count++] = new BallData(s.x, s.y, s.vx, s.vy, s.color, 0);
            }
        }

        public void RemoveDescendingIndices(NativeArray<int> descendingIndices)
        {
            if (!descendingIndices.IsCreated || descendingIndices.Length == 0) return;
            RemoveDescendingIndicesInternal(descendingIndices.Slice());
        }

        public void RemoveDescendingIndices(NativeSlice<int> descendingIndices)
        {
            if (descendingIndices.Length == 0) return;
            RemoveDescendingIndicesInternal(descendingIndices);
        }

        public void RemoveDescendingIndices(NativeArray<uint> descendingIndices)
        {
            if (!descendingIndices.IsCreated || descendingIndices.Length == 0) return;
            int last = m_Count;
            for (int i = 0; i < descendingIndices.Length; ++i)
            {
                int index = (int)descendingIndices[i];
                --last;
                if (index != last)
                {
                    m_Balls[index] = m_Balls[last];
                }
            }
            m_Count = last;
        }

        public void RemoveDescendingIndices(NativeSlice<uint> descendingIndices)
        {
            if (descendingIndices.Length == 0) return;
            int last = m_Count;
            for (int i = 0; i < descendingIndices.Length; ++i)
            {
                int index = (int)descendingIndices[i];
                --last;
                if (index != last)
                {
                    m_Balls[index] = m_Balls[last];
                }
            }
            m_Count = last;
        }

        private void RemoveDescendingIndicesInternal(NativeSlice<int> descendingIndices)
        {
            int last = m_Count;
            for (int i = 0; i < descendingIndices.Length; ++i)
            {
                int index = descendingIndices[i];
                --last;
                if (index != last)
                {
                    m_Balls[index] = m_Balls[last];
                }
            }
            m_Count = last;
        }

        public int Truncate(int newLimit)
        {
            if (newLimit < 0) newLimit = 0;
            if (newLimit < m_Count)
            {
                int removed = m_Count - newLimit;
                m_Count = newLimit;
                return removed;
            }
            return 0;
        }

        public int Truncate(int newLimit, NativeArray<ulong> removedTally)
        {
            if (newLimit < 0) newLimit = 0;
            if (newLimit >= m_Count) return 0;

            int removedCount = m_Count - newLimit;
            if (removedTally.IsCreated)
            {
                for (int i = newLimit; i < m_Count; ++i)
                {
                    int c = m_Balls[i].color;
                    if (c >= 0 && c < removedTally.Length)
                    {
                        removedTally[c] += 1;
                    }
                }
            }
            m_Count = newLimit;
            return removedCount;
        }

        public void Clear()
        {
            m_Count = 0;
        }

        public void PackForRender(NativeArray<uint> destination)
        {
            float scaleX = BallStep.renderPackScale(fieldWidth);
            float scaleY = BallStep.renderPackScale(fieldHeight);
            PackForRender(destination, scaleX, scaleY);
        }

        public void PackForRender(NativeArray<uint> destination, float scaleX, float scaleY)
        {
            int n = math.min(m_Count, destination.Length);
            for (int i = 0; i < n; ++i)
            {
                BallData b = m_Balls[i];
                destination[i] = BallStep.packBallForRender(b.x, b.y, b.color, scaleX, scaleY);
            }
        }

        public void Dispose()
        {
            if (m_Balls.IsCreated)
            {
                m_Balls.Dispose();
            }
            m_Count = 0;
            m_Capacity = 0;
        }
    }
}
