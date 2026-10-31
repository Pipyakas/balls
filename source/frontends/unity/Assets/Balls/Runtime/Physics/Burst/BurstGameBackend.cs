using System;
using System.Collections.Generic;
using System.Diagnostics;
using Balls.Ecs;
using Balls.Game;
using Balls.Native;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using Unity.Jobs;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{
    /// <summary>
    /// Pure C# / Burst implementation of the GameBackend interface.
    /// Runs game session rules and physics entirely on CPU worker threads via Unity Burst jobs,
    /// enabling complete headless execution without balls_unity.dll or the native C++ core.
    ///
    /// Mirrored from:
    /// - source/core/game/GameSession.h / GameSession.cpp (rules, session state, queue rotation, budgets)
    /// - source/core/sim/Simulation.h / Simulation.cpp (tick accumulation, step parameters, hit resolution)
    /// </summary>
    public sealed class BurstGameBackend : GameBackend
    {
        public const int BackendBurst = 5;

        public static readonly int[] SpawnRateOptions = { 10, 50, 200, 1000, 2000, 10000, 50000, 200000, 1000000, 0 };
        public const int BallLimitLevel = 0;
        public const int BallLimitAuto = 1;
        public const int BallLimitMax = 2;
        public const int BallLimitChoiceCount = 3;

        public const float NormalModeSpawnRate = 60.0f;
        public const float TickSeconds = 1.0f / 120.0f;
        public const int MaxTicksPerFrame = 4;
        public const float BallRadius = 0.25f;
        public const float BallSpeed = 14.0f;
        public const int RetargetBounceThreshold = 3;
        public const float LaunchHalfAngleDegrees = 55.0f;

        private readonly int gameMode;
        private readonly LevelData levelData;
        private readonly BurstBoard board;
        private readonly BurstLauncherQueue launcherQueue;
        private readonly BurstSpawnController spawnController;

        private BurstBallStore ballStore;
        private NativeArray<uint> packedBalls;
        private readonly List<BallSpawn> pendingSpawns = new();
        private readonly List<int> killedBalls = new();

        private int sessionState = BallsNative.StateReady;
        private int selectedSpawnRate = 2; // 200 balls/s default
        private int selectedBallLimit = BallLimitLevel; // Level default
        private bool endlessStreams = false;
        private bool autoRelease;
        private double elapsed;
        private float accumulator;
        private uint elapsedTicks;
        private bool machineLimitSelected;
        private ulong measuredMachineLimit;

        private BoardFrame frame;
        private BackendStats stats;
        private Mt19937 random;
        private readonly Stopwatch frameStopwatch = new();

        public override bool IsNative => false;
        public override int Mode => gameMode;
        public override int State => sessionState;
        public override int BackendKind => BackendBurst;
        public override double Elapsed => elapsed;
        public override BoardFrame Frame => frame;
        public override BackendStats Stats => stats;
        public override string LastFault => string.Empty;

        public override int SpawnRateIndex => selectedSpawnRate;
        public override int BallLimitIndex => selectedBallLimit;
        public override bool EndlessSpawn => endlessStreams;
        public override ulong BallLimit => selectedBallLimit == BallLimitMax ? ulong.MaxValue : (measuredMachineLimit != 0 ? measuredMachineLimit : ulong.MaxValue);
        public override bool HasMachineLimit => selectedBallLimit == BallLimitAuto && measuredMachineLimit != 0;
        public override ulong MachineLimit => measuredMachineLimit;

        public bool ReleasesLevelCountsOnly => selectedBallLimit == BallLimitLevel;
        public bool IsUsingMachineBallLimit => selectedBallLimit == BallLimitAuto;

        public BurstBoard Board => board;
        public BurstLauncherQueue Launchers => launcherQueue;
        public BurstSpawnController Spawner => spawnController;
        public BurstBallStore BallStore => ballStore;

        public float LaunchX => (float)board.Columns * 0.5f;
        public float LaunchY => (float)board.Rows - BallRadius * 2.0f;

        public BurstGameBackend(int mode, LevelData level)
        {
            gameMode = mode;
            levelData = level;
            board = new BurstBoard();
            launcherQueue = new BurstLauncherQueue();
            spawnController = new BurstSpawnController();
            ballStore = new BurstBallStore(1024, Allocator.Persistent);
            ballStore.fieldWidth = (float)level.bricks.width;
            ballStore.fieldHeight = (float)level.bricks.playableHeight;
            packedBalls = new NativeArray<uint>(1024, Allocator.Persistent);
            random = Mt19937.Create(20261026u);

            Restart();
        }

        public BurstGameBackend(int mode, string levelDirectory, string levelName, int backendKind = BackendBurst)
            : this(mode, LevelDocument.LoadLevelFromDirectory(levelDirectory, levelName))
        {
        }

        /// <summary>
        /// Resets the game session, reloads level data, and resets state.
        /// Mirrored from source/core/game/GameSession.cpp:15-24.
        /// </summary>
        public override void Restart()
        {
            board.Load(levelData.bricks);
            launcherQueue.Load(levelData.launchers);
            spawnController.Clear();
            endlessStreams = false;
            spawnController.SetInterleaved(gameMode == BallsNative.ModePerformance);
            spawnController.SetBallsPerSecond(gameMode == BallsNative.ModeNormal
                ? NormalModeSpawnRate
                : (float)SpawnRateOptions[selectedSpawnRate]);

            sessionState = gameMode == BallsNative.ModeNormal ? BallsNative.StateRunning : BallsNative.StateReady;
            autoRelease = false;
            elapsed = 0.0;
            accumulator = 0.0f;
            elapsedTicks = 0;

            ballStore.Clear();
            ballStore.fieldWidth = (float)board.Columns;
            ballStore.fieldHeight = (float)board.Rows;
            pendingSpawns.Clear();
            stats = default;
            random = Mt19937.Create(20261026u);

            RefreshFrame();
        }

        /// <summary>
        /// Selects a launcher at (column, row) in Normal gameplay mode.
        /// Mirrored from source/core/game/GameSession.cpp:32-42.
        /// </summary>
        public override bool ChooseLauncher(int column, int row)
        {
            if (gameMode != BallsNative.ModeNormal || sessionState != BallsNative.StateRunning)
            {
                return false;
            }
            var launcher = launcherQueue.Take(column, row);
            if (!launcher.HasValue)
            {
                return false;
            }
            ReleaseLauncher(launcher.Value);
            return true;
        }

        /// <summary>
        /// Starts automatic round-robin release of launchers in Performance mode.
        /// Mirrored from source/core/game/GameSession.cpp:44-49.
        /// </summary>
        public override void StartAutoRelease()
        {
            if (gameMode == BallsNative.ModePerformance && sessionState == BallsNative.StateReady)
            {
                autoRelease = true;
                sessionState = BallsNative.StateRunning;
            }
        }

        public override void SelectSpawnRate(int index)
        {
            selectedSpawnRate = math.clamp(index, 0, SpawnRateOptions.Length - 1);
            if (gameMode == BallsNative.ModePerformance)
            {
                spawnController.SetBallsPerSecond((float)SpawnRateOptions[selectedSpawnRate]);
            }
        }

        public override void SelectBallLimit(int index)
        {
            int choice = math.clamp(index, 0, BallLimitChoiceCount - 1);
            if (choice == BallLimitAuto)
            {
                machineLimitSelected = true;
                measuredMachineLimit = 0;
                selectedBallLimit = BallLimitAuto;
                return;
            }
            machineLimitSelected = false;
            measuredMachineLimit = 0;
            selectedBallLimit = choice;
            if (choice == BallLimitLevel && endlessStreams)
            {
                spawnController.Clear();
                endlessStreams = false;
            }
        }

        public override void UseMachineLimit(ulong limit)
        {
            measuredMachineLimit = limit;
            machineLimitSelected = true;
            selectedBallLimit = BallLimitAuto;
        }

        private void StartEndlessStreams()
        {
            if (endlessStreams)
            {
                return;
            }
            endlessStreams = true;
            var streaming = new bool[BurstBoard.BallColorCount];
            if (levelData?.launchers?.slots != null)
            {
                foreach (var launcher in levelData.launchers.slots)
                {
                    if (!launcher.HasValue)
                    {
                        continue;
                    }
                    int color = launcher.Value.color;
                    if (color >= 0 && color < streaming.Length && !streaming[color])
                    {
                        streaming[color] = true;
                        spawnController.Enqueue(color, ulong.MaxValue / (2UL * (ulong)BurstBoard.BallColorCount));
                    }
                }
            }
        }

        public override bool SwitchBackend(int kind)
        {
            return kind == BackendBurst;
        }

        public bool SwitchBackend(string name)
        {
            return string.Equals(name, "Burst", StringComparison.OrdinalIgnoreCase);
        }

        public override void StartProbe(double targetFps)
        {
            // Capacity probe is specific to hardware-measuring native backends; unsupported in Burst backend.
        }

        public override void ReportFrame(double frameSeconds, double workSeconds)
        {
            // No-op for capacity probe technique.
        }

        /// <summary>
        /// Releases a launcher into the spawn controller.
        /// Mirrored from source/core/game/GameSession.cpp:80-84.
        /// </summary>
        private void ReleaseLauncher(Launcher launcher)
        {
            spawnController.Enqueue(launcher.color, (ulong)launcher.ballCount);
        }

        private static ulong RemainingCapacity(ulong limit, ulong current)
        {
            return limit > current ? limit - current : 0UL;
        }

        /// <summary>
        /// Advances per-frame game session bookkeeping and accumulates physics ticks.
        /// Mirrored from source/core/game/GameSession.cpp:342-362 and source/core/sim/Simulation.cpp:206-218.
        /// </summary>
        public override void Update(float deltaSeconds)
        {
            launcherQueue.Animate(deltaSeconds);
            board.Animate(deltaSeconds);

            if (sessionState == BallsNative.StateRunning)
            {
                elapsed += deltaSeconds;

                if (autoRelease && !ReleasesLevelCountsOnly)
                {
                    StartEndlessStreams();
                }

                while (autoRelease && ReleasesLevelCountsOnly &&
                       spawnController.ActiveStreams < BurstBoard.BallColorCount)
                {
                    var next = launcherQueue.TakeNextInRotation();
                    if (!next.HasValue)
                    {
                        break;
                    }
                    ReleaseLauncher(next.Value);
                }

                ulong limit = gameMode == BallsNative.ModePerformance ? BallLimit : ulong.MaxValue;
                ulong liveCount = (ulong)(ballStore.Count + pendingSpawns.Count);
                spawnController.Update(deltaSeconds, LaunchBalls, RemainingCapacity(limit, liveCount));
            }

            if (sessionState != BallsNative.StateReady)
            {
                frameStopwatch.Restart();
                accumulator = math.min(accumulator + deltaSeconds, TickSeconds * (float)MaxTicksPerFrame);
                int ticks = (int)(accumulator / TickSeconds);
                accumulator -= (float)ticks * TickSeconds;

                if (ticks > 0)
                {
                    Tick(ticks);
                }

                frameStopwatch.Stop();
                stats.ticks = ticks;
                stats.physicsMs = frameStopwatch.Elapsed.TotalMilliseconds;
            }

            if (sessionState == BallsNative.StateRunning && board.AliveBrickCount == 0)
            {
                sessionState = BallsNative.StateWon;
            }

            RefreshFrame();
        }

        /// <summary>
        /// Launches a specified number of balls of a color into pending spawns with random angle and jitter.
        /// Mirrored from source/core/sim/Simulation.cpp:44-59.
        /// </summary>
        private void LaunchBalls(int color, ulong count)
        {
            const float degreesToRadians = (float)(math.PI / 180.0);
            float halfAngle = LaunchHalfAngleDegrees * degreesToRadians;
            float startX = LaunchX;
            float startY = LaunchY;

            for (ulong i = 0; i < count; ++i)
            {
                float angle = random.NextFloatRange(-halfAngle, halfAngle);
                float jitter = random.NextFloatRange(-BallRadius, BallRadius);
                float vx = math.sin(angle) * BallSpeed;
                float vy = -math.cos(angle) * BallSpeed;
                pendingSpawns.Add(new BallSpawn(startX + jitter, startY, vx, vy, color));
            }

            IncrementColor(color, count);
            stats.spawned += count;
        }

        /// <summary>
        /// Injects an exact ball directly into the simulation without randomness.
        /// Required by the parity test harness.
        /// Mirrored from source/core/sim/Simulation.h:42 (spawnBalls).
        /// </summary>
        public void SpawnBallExact(float x, float y, float vx, float vy, int color)
        {
            pendingSpawns.Add(new BallSpawn(x, y, vx, vy, color));
            IncrementColor(color, 1UL);
            stats.spawned++;
            stats.liveBallCount = (ulong)(ballStore.Count + pendingSpawns.Count);
        }

        /// <summary>
        /// Steps the physics simulation by an exact number of ticks without wall-clock coupling.
        /// Required by the parity test harness.
        /// Mirrored from source/core/sim/Simulation.cpp:189-204 (tick).
        /// </summary>
        public void Tick(int ticks)
        {
            if (ticks <= 0) return;

            // 1. Flush pending spawns into ball store
            if (pendingSpawns.Count > 0)
            {
                var spawnsArray = new NativeArray<BallSpawn>(pendingSpawns.Count, Allocator.Temp);
                try
                {
                    for (int i = 0; i < pendingSpawns.Count; ++i)
                    {
                        spawnsArray[i] = pendingSpawns[i];
                    }
                    ballStore.AddBalls(spawnsArray);
                }
                finally
                {
                    spawnsArray.Dispose();
                }
                pendingSpawns.Clear();
            }

            int ballCount = ballStore.Count;
            if (ballCount == 0)
            {
                elapsedTicks += (uint)ticks;
                stats.ballCount = 0;
                stats.liveBallCount = 0;
                return;
            }

            EnsurePackedCapacity(ballStore.Capacity);

            float scaleX = BallStep.renderPackScale((float)board.Columns);
            float scaleY = BallStep.renderPackScale((float)board.Rows);
            int isFreeFlight = (board.AliveBrickCount == 0) ? 1 : 0;

            var stepParams = new StepParams(
                TickSeconds,
                BallRadius,
                BallSpeed,
                (float)board.Columns,
                (float)board.Rows,
                board.PaddedWidth,
                board.PaddedCellCount,
                RetargetBounceThreshold,
                scaleX,
                scaleY,
                ticks,
                elapsedTicks,
                isFreeFlight);

            var hitsQueue = new NativeQueue<BallHit>(Allocator.TempJob);

            var job = new BurstBallStepJob
            {
                balls = ballStore.Balls,
                board = board.BoardView,
                stepParams = stepParams,
                hits = hitsQueue.AsParallelWriter(),
                packedBalls = packedBalls
            };

            JobHandle handle = job.Schedule(ballCount, 64);
            handle.Complete();

            elapsedTicks += (uint)ticks;

            // 2. Resolve hits mirroring source/core/sim/Simulation.cpp:168-187 (resolveHits)
            killedBalls.Clear();
            bool anyDestroyed = false;

            while (hitsQueue.TryDequeue(out BallHit hit))
            {
                int brickIndex = hit.brick;
                if (brickIndex < 0 || brickIndex >= board.BrickCount)
                {
                    continue;
                }

                Brick brick = board.Bricks[brickIndex];
                if (!brick.IsAlive)
                {
                    continue;
                }

                board.ApplyHit(brickIndex);
                if (!brick.IsAlive)
                {
                    anyDestroyed = true;
                }

                killedBalls.Add((int)hit.ball);
                DecrementColor(brick.color, 1UL);
            }

            hitsQueue.Dispose();

            if (killedBalls.Count > 0)
            {
                // Sort descending to allow safe swap-remove in BurstBallStore
                killedBalls.Sort((a, b) => b.CompareTo(a));

                // Deduplicate ball indices if any multiple hits recorded the same ball
                int uniqueCount = 0;
                for (int i = 0; i < killedBalls.Count; ++i)
                {
                    if (i == 0 || killedBalls[i] != killedBalls[i - 1])
                    {
                        killedBalls[uniqueCount++] = killedBalls[i];
                    }
                }

                var descendingIndices = new NativeArray<int>(uniqueCount, Allocator.Temp);
                for (int i = 0; i < uniqueCount; ++i)
                {
                    descendingIndices[i] = killedBalls[i];
                }

                // Sync packedBalls swap-removal alongside ballStore
                int last = ballStore.Count;
                for (int i = 0; i < uniqueCount; ++i)
                {
                    int index = descendingIndices[i];
                    --last;
                    if (index != last && packedBalls.IsCreated && last < packedBalls.Length && index < packedBalls.Length)
                    {
                        packedBalls[index] = packedBalls[last];
                    }
                }

                ballStore.RemoveDescendingIndices(descendingIndices);
                descendingIndices.Dispose();
                stats.hits += (ulong)uniqueCount;
            }

            if (anyDestroyed)
            {
                board.SettleColumns();
            }

            stats.ballCount = (ulong)ballStore.Count;
            stats.liveBallCount = (ulong)(ballStore.Count + pendingSpawns.Count);
        }

        private void EnsurePackedCapacity(int capacity)
        {
            if (packedBalls.IsCreated && packedBalls.Length >= capacity)
            {
                return;
            }
            if (packedBalls.IsCreated)
            {
                packedBalls.Dispose();
            }
            packedBalls = new NativeArray<uint>(math.max(1024, capacity), Allocator.Persistent);
        }

        private void IncrementColor(int color, ulong count)
        {
            switch (color)
            {
                case 0: stats.red += count; break;
                case 1: stats.orange += count; break;
                case 2: stats.yellow += count; break;
                case 3: stats.green += count; break;
                case 4: stats.blue += count; break;
                case 5: stats.indigo += count; break;
                case 6: stats.violet += count; break;
            }
        }

        private void DecrementColor(int color, ulong count = 1UL)
        {
            switch (color)
            {
                case 0: stats.red = stats.red >= count ? stats.red - count : 0; break;
                case 1: stats.orange = stats.orange >= count ? stats.orange - count : 0; break;
                case 2: stats.yellow = stats.yellow >= count ? stats.yellow - count : 0; break;
                case 3: stats.green = stats.green >= count ? stats.green - count : 0; break;
                case 4: stats.blue = stats.blue >= count ? stats.blue - count : 0; break;
                case 5: stats.indigo = stats.indigo >= count ? stats.indigo - count : 0; break;
                case 6: stats.violet = stats.violet >= count ? stats.violet - count : 0; break;
            }
        }

        private void RefreshFrame()
        {
            frame = new BoardFrame
            {
                columns = board.Columns,
                rows = board.Rows,
                playableHeight = board.PlayableHeight,
                paddedWidth = board.PaddedWidth,
                paddedCellCount = board.PaddedCellCount,
                brickCount = board.BrickCount,
                aliveBrickCount = board.AliveBrickCount,
                revision = board.Revision,
                launcherColumns = launcherQueue.ColumnCount,
                launcherVisibleRows = launcherQueue.VisibleRows,
                launcherRemainingBalls = launcherQueue.TotalRemainingBalls()
            };
        }

        public override void ReadCells(List<CellElement> destination)
        {
            int count = frame.paddedCellCount;
            while (destination.Count < count)
            {
                destination.Add(default);
            }
            if (destination.Count > count)
            {
                destination.RemoveRange(count, destination.Count - count);
            }
            if (count == 0)
            {
                return;
            }

            for (int index = 0; index < count; ++index)
            {
                int brickIndex = board.Cell(index);
                bool occupied = brickIndex >= 0 && brickIndex < board.BrickCount;
                destination[index] = new CellElement
                {
                    brick = brickIndex,
                    color = occupied ? board.BrickColor(brickIndex) : 0,
                    hitsLeft = occupied ? board.BrickHitsLeft(brickIndex) : 0,
                    rise = occupied ? board.BrickRiseOffset(brickIndex) : 0f,
                    flash = occupied ? board.BrickHitFlash(brickIndex) : 0f,
                };
            }
        }

        public override void ReadLaunchers(List<LauncherElement> destination)
        {
            int count = frame.launcherColumns * Math.Max(1, frame.launcherVisibleRows);
            while (destination.Count < count)
            {
                destination.Add(default);
            }
            if (destination.Count > count)
            {
                destination.RemoveRange(count, destination.Count - count);
            }

            int slot = 0;
            for (int row = 0; row < frame.launcherVisibleRows; ++row)
            {
                for (int column = 0; column < frame.launcherColumns; ++column, ++slot)
                {
                    var queued = launcherQueue.Visible(column, row);
                    destination[slot] = new LauncherElement
                    {
                        column = column,
                        row = row,
                        color = queued != null ? queued.launcher.color : 0,
                        ballCount = queued != null ? queued.launcher.ballCount : 0,
                        rise = queued != null ? queued.riseOffset : 0f,
                        present = queued != null,
                    };
                }
            }
        }

        public override int ReadBalls(out IntPtr packed, out float scaleX, out float scaleY)
        {
            if (ballStore.Count == 0 || !packedBalls.IsCreated)
            {
                packed = IntPtr.Zero;
                scaleX = 1f;
                scaleY = 1f;
                return 0;
            }

            unsafe
            {
                packed = (IntPtr)NativeArrayUnsafeUtility.GetUnsafePtr(packedBalls);
            }
            scaleX = BallStep.renderPackScale((float)board.Columns);
            scaleY = BallStep.renderPackScale((float)board.Rows);
            return ballStore.Count;
        }

        public override void Dispose()
        {
            board.Dispose();
            ballStore.Dispose();
            if (packedBalls.IsCreated)
            {
                packedBalls.Dispose();
            }
        }

        /// <summary>
        /// Exact 32-bit Mersenne Twister (MT19937) pseudo-random generator.
        /// Mirrored from C++ std::mt19937 with seed 20261026u (Simulation.cpp:32).
        /// </summary>
        private struct Mt19937
        {
            private const int N = 624;
            private const int M = 397;
            private const uint MatrixA = 0x9908B0DFU;
            private const uint UpperMask = 0x80000000U;
            private const uint LowerMask = 0x7FFFFFFFU;

            private uint[] mt;
            private int mti;

            public static Mt19937 Create(uint seed)
            {
                var rng = new Mt19937();
                rng.mt = new uint[N];
                rng.mt[0] = seed;
                for (rng.mti = 1; rng.mti < N; ++rng.mti)
                {
                    rng.mt[rng.mti] = 1812433253U * (rng.mt[rng.mti - 1] ^ (rng.mt[rng.mti - 1] >> 30)) + (uint)rng.mti;
                }
                return rng;
            }

            public uint NextUInt()
            {
                uint y;
                uint[] mag01 = { 0x0U, MatrixA };

                if (mti >= N)
                {
                    int kk;
                    for (kk = 0; kk < N - M; ++kk)
                    {
                        y = (mt[kk] & UpperMask) | (mt[kk + 1] & LowerMask);
                        mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
                    }
                    for (; kk < N - 1; ++kk)
                    {
                        y = (mt[kk] & UpperMask) | (mt[kk + 1] & LowerMask);
                        mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
                    }
                    y = (mt[N - 1] & UpperMask) | (mt[0] & LowerMask);
                    mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
                    mti = 0;
                }

                y = mt[mti++];
                y ^= (y >> 11);
                y ^= (y << 7) & 0x9D2C5680U;
                y ^= (y << 15) & 0xEFC60000U;
                y ^= (y >> 18);
                return y;
            }

            public float NextFloatRange(float min, float max)
            {
                float unit = (float)(NextUInt() * (1.0 / 4294967296.0));
                return min + (max - min) * unit;
            }
        }
    }
}
