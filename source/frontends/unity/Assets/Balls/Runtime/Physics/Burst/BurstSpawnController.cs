using System;
using System.Collections.Generic;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{
    /// <summary>
    /// Governs ball release rates and per-second budgets.
    /// Mirrored from source/core/game/SpawnController.h / SpawnController.cpp (lines 11-46).
    /// </summary>
    public sealed class BurstSpawnController
    {
        public const ulong MaxBallsPerFrame = 1UL << 20; // 1,048,576
        public const ulong UnlimitedBalls = ulong.MaxValue;

        public struct Stream
        {
            public int color;
            public ulong remaining;

            public Stream(int color, ulong remaining)
            {
                this.color = color;
                this.remaining = remaining;
            }
        }

        private readonly List<Stream> streams = new();
        private float ballsPerSecond = 60.0f;
        private double budget = 0.0;
        private ulong pending = 0;

        public ulong PendingBalls => pending;
        public bool IsIdle => streams.Count == 0;
        public float BallsPerSecond => ballsPerSecond;

        /// <summary>
        /// Enqueues a stream of balls of a specified color to be spawned.
        /// Mirrored from source/core/game/SpawnController.cpp:11-17.
        /// </summary>
        public void Enqueue(int color, ulong count)
        {
            if (count == 0)
            {
                return;
            }
            streams.Add(new Stream(color, count));
            pending = pending > UnlimitedBalls - count ? UnlimitedBalls : pending + count;
        }

        /// <summary>
        /// Clears all queued spawn streams and resets the rate budget.
        /// Mirrored from source/core/game/SpawnController.cpp:19-23.
        /// </summary>
        public void Clear()
        {
            streams.Clear();
            pending = 0;
            budget = 0.0;
        }

        /// <summary>
        /// Sets the spawn rate in balls per second. Non-positive values (<= 0) mean unlimited.
        /// </summary>
        public void SetBallsPerSecond(float rate)
        {
            ballsPerSecond = rate;
        }

        /// <summary>
        /// Advances the spawn budget and invokes launchCallback for released balls up to capacityLeft.
        /// Mirrored from source/core/game/SpawnController.cpp:25-45.
        /// </summary>
        public void Update(float deltaTime, Action<int, ulong> launchCallback, ulong capacityLeft)
        {
            if (streams.Count == 0)
            {
                budget = 0.0;
                return;
            }

            bool unlimited = ballsPerSecond <= 0.0f;
            budget = unlimited
                ? (double)MaxBallsPerFrame
                : budget + (double)ballsPerSecond * deltaTime;

            ulong budgetAllowance = (ulong)math.max(0.0, math.floor(budget));
            ulong allowance = math.min(budgetAllowance, MaxBallsPerFrame);
            allowance = math.min(allowance, capacityLeft);

            budget -= (double)allowance;

            while (allowance > 0 && streams.Count > 0)
            {
                Stream stream = streams[0];
                ulong released = math.min(allowance, stream.remaining);

                launchCallback(stream.color, released);

                stream.remaining -= released;
                allowance -= released;
                pending -= released;

                if (stream.remaining == 0)
                {
                    streams.RemoveAt(0);
                }
                else
                {
                    streams[0] = stream;
                }
            }

            if (unlimited || streams.Count == 0)
            {
                budget = 0.0;
            }
        }
    }
}
