using System;
using System.Collections.Generic;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

    public sealed class BurstSpawnController
    {
        public const ulong MaxBallsPerFrame = 1UL << 20;
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
        private bool interleaved = false;
        private double budget = 0.0;
        private ulong pending = 0;

        public ulong PendingBalls => pending;
        public bool IsIdle => streams.Count == 0;
        public int ActiveStreams => streams.Count;
        public float BallsPerSecond => ballsPerSecond;

        public void SetInterleaved(bool enabled) => interleaved = enabled;

        public void Enqueue(int color, ulong count)
        {
            if (count == 0)
            {
                return;
            }
            streams.Add(new Stream(color, count));
            pending = pending > UnlimitedBalls - count ? UnlimitedBalls : pending + count;
        }

        public void Clear()
        {
            streams.Clear();
            pending = 0;
            budget = 0.0;
        }

        public void SetBallsPerSecond(float rate)
        {
            ballsPerSecond = rate;
            budget = 0.0;
        }

        public void Update(float deltaTime, Action<int, ulong> launchCallback, ulong capacityLeft)
        {
            if (streams.Count == 0)
            {
                budget = 0.0;
                return;
            }

            bool unlimited = ballsPerSecond <= 0.0f;
            budget = unlimited
                ? (double)capacityLeft
                : budget + (double)ballsPerSecond * deltaTime;

            ulong budgetAllowance = (ulong)math.max(0.0, math.floor(budget));
            ulong allowance = math.min(budgetAllowance, MaxBallsPerFrame);
            allowance = math.min(allowance, capacityLeft);
            allowance = math.min(allowance, pending);

            ulong releasedTotal = allowance;

            while (allowance > 0 && streams.Count > 0)
            {
                ulong share = interleaved
                    ? math.max(1UL, allowance / (ulong)streams.Count)
                    : allowance;

                for (int i = 0; i < streams.Count && allowance > 0;)
                {
                    Stream stream = streams[i];
                    ulong launched = math.min(share, allowance);
                    launched = math.min(launched, stream.remaining);

                    launchCallback(stream.color, launched);

                    stream.remaining -= launched;
                    allowance -= launched;
                    pending -= launched;

                    if (stream.remaining == 0)
                    {
                        streams.RemoveAt(i);
                    }
                    else
                    {
                        streams[i] = stream;
                        i++;
                    }
                }
            }

            budget -= (double)(releasedTotal - allowance);

            if (interleaved && streams.Count > 1)
            {
                Stream first = streams[0];
                streams.RemoveAt(0);
                streams.Add(first);
            }

            if (unlimited || streams.Count == 0)
            {
                budget = 0.0;
            }
        }
    }
}
