using Unity.Burst;
using Unity.Collections;
using Unity.Jobs;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{
    /// <summary>
    /// Event recorded when a ball collides with a brick of the matching color.
    /// Mirrored from C++ struct BallHit in source/core/sim/PhysicsBackend.h.
    /// </summary>
    public struct BallHit
    {
        public uint ball;
        public int brick;

        public BallHit(uint ball, int brick)
        {
            this.ball = ball;
            this.brick = brick;
        }
    }

    /// <summary>
    /// Burst-compiled parallel job that steps balls for one simulation tick.
    /// Uses BoardView over NativeArrays for grid collision checks and writes
    /// detected hits into a thread-safe NativeQueue.ParallelWriter.
    /// Supports substeps from StepParams.substeps via BallStep.stepBallSubsteps.
    /// </summary>
    [BurstCompile(FloatMode = FloatMode.Fast, FloatPrecision = FloatPrecision.Standard, CompileSynchronously = true)]
    public struct BurstBallStepJob : IJobParallelFor
    {
        /// <summary>Active ball storage (positions, velocities, color, bounces).</summary>
        public NativeArray<BallData> balls;

        /// <summary>Read-only board grid and target field.</summary>
        [ReadOnly] public BoardView board;

        /// <summary>Step parameters (deltaTime, radius, speed, dimensions, substeps, etc.).</summary>
        public StepParams stepParams;

        /// <summary>Parallel writer queue for collecting hits across worker threads.</summary>
        public NativeQueue<BallHit>.ParallelWriter hits;

        /// <summary>
        /// Optional destination for packed render coordinates.
        /// If allocated, each ball is packed directly during the step pass
        /// to eliminate a separate packing pass.
        /// </summary>
        [NativeDisableParallelForRestriction]
        public NativeArray<uint> packedBalls;

        public void Execute(int index)
        {
            BallData ball = balls[index];

            // Step ball with substep support mirroring ScalarBackend.cpp and BallStepCore.h:254-263
            int hitBrick = BallStep.stepBallSubsteps(
                ref ball.x,
                ref ball.y,
                ref ball.vx,
                ref ball.vy,
                ball.color,
                ref ball.bounces,
                board,
                stepParams);

            balls[index] = ball;

            // Optional direct render packing
            if (packedBalls.IsCreated && index < packedBalls.Length)
            {
                packedBalls[index] = BallStep.packBallForRender(
                    ball.x,
                    ball.y,
                    ball.color,
                    stepParams.packScaleX,
                    stepParams.packScaleY);
            }

            // Report brick hit if any occurred during substeps
            if (hitBrick != BallStep.noBrickHit)
            {
                hits.Enqueue(new BallHit((uint)index, hitBrick));
            }
        }

        /// <summary>
        /// Schedules the parallel ball step job across available Burst worker threads.
        /// </summary>
        public static JobHandle Schedule(
            NativeArray<BallData> balls,
            BoardView board,
            StepParams stepParams,
            NativeQueue<BallHit>.ParallelWriter hits,
            NativeArray<uint> packedBalls = default,
            int batchSize = 64,
            JobHandle dependency = default)
        {
            var job = new BurstBallStepJob
            {
                balls = balls,
                board = board,
                stepParams = stepParams,
                hits = hits,
                packedBalls = packedBalls
            };
            return job.Schedule(balls.Length, batchSize, dependency);
        }
    }

    /// <summary>
    /// Burst-compiled parallel job that evaluates free-flight closed-form movement
    /// for all balls when the board is cleared (params.freeFlight != 0).
    /// </summary>
    [BurstCompile(FloatMode = FloatMode.Fast, FloatPrecision = FloatPrecision.Standard, CompileSynchronously = true)]
    public struct BurstFreeFlightJob : IJobParallelFor
    {
        public NativeArray<BallData> balls;
        public StepParams stepParams;
        public uint elapsedTicks;

        [NativeDisableParallelForRestriction]
        public NativeArray<uint> packedBalls;

        public void Execute(int index)
        {
            BallData ball = balls[index];
            FreeFlightPosition flown = BallStep.flyFreely(
                ball.x,
                ball.y,
                ball.vx,
                ball.vy,
                elapsedTicks,
                stepParams);

            ball.x = flown.x;
            ball.y = flown.y;
            ball.vx = flown.vx;
            ball.vy = flown.vy;
            ball.bounces += (int)flown.wallCrossings;
            balls[index] = ball;

            if (packedBalls.IsCreated && index < packedBalls.Length)
            {
                packedBalls[index] = BallStep.packBallForRender(
                    ball.x,
                    ball.y,
                    ball.color,
                    stepParams.packScaleX,
                    stepParams.packScaleY);
            }
        }

        public static JobHandle Schedule(
            NativeArray<BallData> balls,
            StepParams stepParams,
            uint elapsedTicks,
            NativeArray<uint> packedBalls = default,
            int batchSize = 64,
            JobHandle dependency = default)
        {
            var job = new BurstFreeFlightJob
            {
                balls = balls,
                stepParams = stepParams,
                elapsedTicks = elapsedTicks,
                packedBalls = packedBalls
            };
            return job.Schedule(balls.Length, batchSize, dependency);
        }
    }

    /// <summary>
    /// Burst-compiled parallel job to pack all balls for rendering.
    /// </summary>
    [BurstCompile(FloatMode = FloatMode.Fast, FloatPrecision = FloatPrecision.Standard, CompileSynchronously = true)]
    public struct BurstPackBallsJob : IJobParallelFor
    {
        [ReadOnly] public NativeArray<BallData> balls;
        public float scaleX;
        public float scaleY;
        [WriteOnly] public NativeArray<uint> packedBalls;

        public void Execute(int index)
        {
            BallData ball = balls[index];
            packedBalls[index] = BallStep.packBallForRender(ball.x, ball.y, ball.color, scaleX, scaleY);
        }

        public static JobHandle Schedule(
            NativeArray<BallData> balls,
            float scaleX,
            float scaleY,
            NativeArray<uint> packedBalls,
            int batchSize = 128,
            JobHandle dependency = default)
        {
            var job = new BurstPackBallsJob
            {
                balls = balls,
                scaleX = scaleX,
                scaleY = scaleY,
                packedBalls = packedBalls
            };
            return job.Schedule(balls.Length, batchSize, dependency);
        }
    }
}
