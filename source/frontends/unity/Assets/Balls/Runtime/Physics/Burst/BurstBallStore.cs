using System;
using System.Runtime.CompilerServices;
using Unity.Burst;
using Unity.Collections;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{
    // =========================================================================================================
    // ARCHITECTURE NOTE: AoS (Array of Structures) vs. SoA (Structure of Arrays) Justification
    //
    // In this Burst implementation, an AoS layout (NativeArray<BallData>) was chosen over SoA for these reasons:
    //
    // 1. Memory Allocator & Lifetime Management:
    //    AoS requires managing exactly ONE unmanaged buffer allocation (NativeArray<BallData>).
    //    SoA would require allocating, resizing, tracking, and disposing 6 separate NativeArrays
    //    (x, y, vx, vy, color, bounces). In Unity/C#, managing multiple NativeArrays incurs safety handle tracking
    //    overhead in Editor play mode, increases memory fragmentation, and complicates dynamic resizing.
    //
    // 2. CPU Cache Locality During Physics Stepping:
    //    In C++, CpuBallStore used SoA primarily to feed explicit 8-wide AVX2 vector registers (__m256) where 8
    //    balls were stepped simultaneously across parallel SIMD lanes (Avx2Backend.cpp).
    //    In Unity Burst, physics stepping is parallelized across worker threads via IJobParallelFor chunking.
    //    Each worker thread processes balls in scalar loops. A BallData struct is 24 bytes (4 floats, 2 ints).
    //    One 64-byte L1 CPU cache line holds 2.66 complete balls. When a thread accesses ball i, all 6 simulation
    //    properties reside in the same cache line with zero strided cache misses across 6 distinct buffers.
    //
    // 3. Constant-Time Descending Swap-Back Removal:
    //    When removing balls via RemoveDescendingIndices, AoS performs a single 24-byte struct copy per removal
    //    (balls[index] = balls[last]). With SoA, every removal requires 6 separate indexed memory writes across
    //    6 separate array pointers.
    //
    // 4. Job Struct Ergonomics & Safety Constraints:
    //    Passing a single NativeArray<BallData> to BurstBallStepJob minimizes job parameter overhead and avoids
    //    hitting container safety limits in Unity Jobs.
    // =========================================================================================================

    /// <summary>
    /// Per-ball physical state (24 bytes).
    /// </summary>
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

    /// <summary>
    /// Initial ball spawn parameters (mirrors C++ BallSpawn in PhysicsBackend.h).
    /// </summary>
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

    /// <summary>
    /// Storage for balls simulated by Burst, backed by a contiguous NativeArray of BallData.
    /// Provides AddBall, RemoveDescendingIndices, Truncate, Count, and PackForRender.
    /// </summary>
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

        /// <summary>Raw backing array up to Capacity.</summary>
        public NativeArray<BallData> Balls => m_Balls;

        /// <summary>Active slice covering balls 0..Count-1.</summary>
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

        /// <summary>
        /// Ensures backing buffer has room for at least requiredCapacity balls, growing exponentially.
        /// </summary>
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

        /// <summary>
        /// Adds a single ball to the store.
        /// </summary>
        public void AddBall(float x, float y, float vx, float vy, int color, int bounces = 0)
        {
            EnsureCapacity(m_Count + 1);
            m_Balls[m_Count++] = new BallData(x, y, vx, vy, color, bounces);
        }

        /// <summary>
        /// Adds a single ball from spawn parameters.
        /// </summary>
        public void AddBall(BallSpawn spawn)
        {
            AddBall(spawn.x, spawn.y, spawn.vx, spawn.vy, spawn.color, 0);
        }

        /// <summary>
        /// Adds a batch of balls from a NativeArray of BallSpawn.
        /// </summary>
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

        /// <summary>
        /// Adds a batch of balls from a NativeSlice of BallSpawn.
        /// </summary>
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

        /// <summary>
        /// Removes balls at indices that MUST be sorted in strictly descending order.
        /// Replicates the C++ CpuBallStore::swapRemove trick:
        /// By iterating descending indices and swapping with the decrementing last element,
        /// any swapped element originates from an index >= current removal index, guaranteeing
        /// that already swapped elements are never accidentally overwritten.
        /// </summary>
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

        /// <summary>
        /// Truncates active balls down to newLimit, returning how many were removed.
        /// </summary>
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

        /// <summary>
        /// Truncates active balls down to newLimit and tallies removed balls by color into removedTally,
        /// exactly mirroring C++ CpuBallStore::truncate.
        /// </summary>
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

        /// <summary>
        /// Clears all active balls without deallocating backing storage.
        /// </summary>
        public void Clear()
        {
            m_Count = 0;
        }

        /// <summary>
        /// Packs all active balls into destination using store's fieldWidth and fieldHeight scales,
        /// matching C++ packBallForRender output layout bit-for-bit:
        /// bits 0-13 x, 14-27 y, 28-31 color.
        /// </summary>
        public void PackForRender(NativeArray<uint> destination)
        {
            float scaleX = BallStep.renderPackScale(fieldWidth);
            float scaleY = BallStep.renderPackScale(fieldHeight);
            PackForRender(destination, scaleX, scaleY);
        }

        /// <summary>
        /// Packs all active balls into destination using explicit coordinate scales.
        /// </summary>
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
