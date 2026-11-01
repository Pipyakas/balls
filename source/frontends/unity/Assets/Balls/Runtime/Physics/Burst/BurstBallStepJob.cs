using Unity.Burst;
using Unity.Collections;
using Unity.Jobs;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

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

    [BurstCompile(FloatMode = FloatMode.Fast, FloatPrecision = FloatPrecision.Standard, CompileSynchronously = true)]
    public struct BurstBallStepJob : IJobParallelFor
    {

        public NativeArray<BallData> balls;

        [ReadOnly] public BoardView board;

        public StepParams stepParams;

        public NativeQueue<BallHit>.ParallelWriter hits;

        [NativeDisableParallelForRestriction]
        public NativeArray<uint> packedBalls;

        public void Execute(int index)
        {
            BallData ball = balls[index];

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

            if (packedBalls.IsCreated && index < packedBalls.Length)
            {
                packedBalls[index] = BallStep.packBallForRender(
                    ball.x,
                    ball.y,
                    ball.color,
                    stepParams.packScaleX,
                    stepParams.packScaleY);
            }

            if (hitBrick != BallStep.noBrickHit)
            {
                hits.Enqueue(new BallHit((uint)index, hitBrick));
            }
        }

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
