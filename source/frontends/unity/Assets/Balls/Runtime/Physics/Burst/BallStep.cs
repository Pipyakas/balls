using System.Runtime.CompilerServices;
using Unity.Burst;
using Unity.Collections;
using Unity.Mathematics;

namespace Balls.Physics.Burst
{

    public struct StepParams
    {
        public float deltaTime;
        public float radius;
        public float speed;
        public float fieldWidth;
        public float fieldHeight;
        public int paddedWidth;
        public int paddedCellCount;
        public int retargetBounceThreshold;
        public float packScaleX;
        public float packScaleY;
        public int substeps;

        public uint tick;

        public int freeFlight;

        public StepParams(
            float deltaTime,
            float radius,
            float speed,
            float fieldWidth,
            float fieldHeight,
            int paddedWidth,
            int paddedCellCount,
            int retargetBounceThreshold,
            float packScaleX,
            float packScaleY,
            int substeps = 1,
            uint tick = 0,
            int freeFlight = 0)
        {
            this.deltaTime = deltaTime;
            this.radius = radius;
            this.speed = speed;
            this.fieldWidth = fieldWidth;
            this.fieldHeight = fieldHeight;
            this.paddedWidth = paddedWidth;
            this.paddedCellCount = paddedCellCount;
            this.retargetBounceThreshold = retargetBounceThreshold;
            this.packScaleX = packScaleX;
            this.packScaleY = packScaleY;
            this.substeps = substeps;
            this.tick = tick;
            this.freeFlight = freeFlight;
        }
    }

    public struct BoardView
    {
        [ReadOnly] public NativeArray<int> paddedCells;
        [ReadOnly] public NativeArray<int> brickColors;
        [ReadOnly] public NativeArray<int> targetCells;
        [ReadOnly] public NativeArray<int> nearBrickCells;

        public BoardView(
            NativeArray<int> paddedCells,
            NativeArray<int> brickColors,
            NativeArray<int> targetCells,
            NativeArray<int> nearBrickCells)
        {
            this.paddedCells = paddedCells;
            this.brickColors = brickColors;
            this.targetCells = targetCells;
            this.nearBrickCells = nearBrickCells;
        }

        public bool IsCreated => paddedCells.IsCreated && brickColors.IsCreated && nearBrickCells.IsCreated;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public bool isNearBrick(int index)
        {
            return nearBrickCells[index] != 0;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public int occupant(int index, out int color)
        {
            int brick = paddedCells[index];
            color = brick < 0 ? -1 : brickColors[brick];
            return brick;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public int targetCell(int color, int index, int cellCount)
        {
            return targetCells[color * cellCount + index];
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public bool IsNearBrick(int index) => isNearBrick(index);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public int Occupant(int index, out int color) => occupant(index, out color);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public int TargetCell(int color, int index, int cellCount) => targetCell(color, index, cellCount);
    }

    public struct FreeFlightPosition
    {
        public float x;
        public float y;
        public float vx;
        public float vy;
        public uint wallCrossings;
    }

    [BurstCompile]
    public static class BallStep
    {
        public const int noBrickHit = -1;
        public const uint renderCoordinateMax = 16383u;
        public const uint renderCoordinateBits = 14u;
        public const uint renderColorShift = 28u;
        public const uint emptyRenderSlot = 0xFFFFFFFFu;

        public const float freeFlightPhaseUnits = 4294967296.0f;
        public const uint freeFlightPhaseHalf = 0x80000000u;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float renderPackScale(float fieldExtent)
        {
            return (float)renderCoordinateMax / (fieldExtent > 0.0f ? fieldExtent : 1.0f);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint packRenderCoordinate(float value, float scale)
        {
            float scaled = value * scale;
            float clamped = scaled < 0.0f ? 0.0f : (scaled > (float)renderCoordinateMax ? (float)renderCoordinateMax : scaled);
            return (uint)(clamped + 0.5f);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint packBallForRender(float x, float y, int color, float scaleX, float scaleY)
        {
            return packRenderCoordinate(x, scaleX) |
                   (packRenderCoordinate(y, scaleY) << (int)renderCoordinateBits) |
                   ((uint)color << (int)renderColorShift);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float clampFloat(float value, float low, float high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int paddedCellIndex(int column, int row, int paddedWidth)
        {
            return (row + 1) * paddedWidth + column + 1;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void pushOutOfCell(
            ref float x,
            ref float y,
            out float normalX,
            out float normalY,
            int column,
            int row,
            float radius)
        {
            float toLeft = x - (float)column;
            float toRight = (float)(column + 1) - x;
            float toTop = y - (float)row;
            float toBottom = (float)(row + 1) - y;
            float nearest = math.min(math.min(toLeft, toRight), math.min(toTop, toBottom));
            normalX = 0.0f;
            normalY = 0.0f;
            if (nearest == toBottom)
            {
                normalY = 1.0f;
                y = (float)(row + 1) + radius;
            }
            else if (nearest == toTop)
            {
                normalY = -1.0f;
                y = (float)row - radius;
            }
            else if (nearest == toLeft)
            {
                normalX = -1.0f;
                x = (float)column - radius;
            }
            else
            {
                normalX = 1.0f;
                x = (float)(column + 1) + radius;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void retargetTowardNearestBrick(
            float x,
            float y,
            ref float vx,
            ref float vy,
            int color,
            float normalX,
            float normalY,
            in BoardView board,
            in StepParams @params)
        {
            int column = (int)math.floor(x);
            int row = (int)math.floor(y);
            int target = board.targetCell(color, paddedCellIndex(column, row, @params.paddedWidth), @params.paddedCellCount);
            if (target < 0)
            {
                return;
            }
            float targetX = (float)(target % @params.paddedWidth) - 0.5f;
            float targetY = (float)(target / @params.paddedWidth) - 0.5f;
            float toTargetX = targetX - x;
            float toTargetY = targetY - y;
            float distance = math.sqrt(toTargetX * toTargetX + toTargetY * toTargetY);
            if (distance < 1e-4f)
            {
                return;
            }
            float directionX = toTargetX / distance;
            float directionY = toTargetY / distance;
            if (directionX * normalX + directionY * normalY < 0.0f)
            {
                return;
            }
            vx = directionX * @params.speed;
            vy = directionY * @params.speed;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void reflectVelocity(ref float vx, ref float vy, float normalX, float normalY)
        {
            float velocityAlongNormal = vx * normalX + vy * normalY;
            if (velocityAlongNormal < 0.0f)
            {
                vx -= 2.0f * velocityAlongNormal * normalX;
                vy -= 2.0f * velocityAlongNormal * normalY;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void resolveEmbeddedContact(
            ref float x,
            ref float y,
            ref float vx,
            ref float vy,
            int color,
            ref int bounces,
            int column,
            int row,
            in BoardView board,
            in StepParams @params)
        {
            float normalX;
            float normalY;
            pushOutOfCell(ref x, ref y, out normalX, out normalY, column, row, @params.radius);
            reflectVelocity(ref vx, ref vy, normalX, normalY);
            bounces += 1;
            if (bounces >= @params.retargetBounceThreshold)
            {
                retargetTowardNearestBrick(x, y, ref vx, ref vy, color, normalX, normalY, board, @params);
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int stepBall(
            ref float x,
            ref float y,
            ref float vx,
            ref float vy,
            int color,
            ref int bounces,
            in BoardView board,
            in StepParams @params)
        {
            float radius = @params.radius;
            x += vx * @params.deltaTime;
            y += vy * @params.deltaTime;

            float normalX = 0.0f;
            float normalY = 0.0f;
            bool bounced = false;
            if (x < radius)
            {
                x = 2.0f * radius - x;
                vx = math.abs(vx);
                normalX = 1.0f;
                bounced = true;
            }
            else if (x > @params.fieldWidth - radius)
            {
                x = 2.0f * (@params.fieldWidth - radius) - x;
                vx = -math.abs(vx);
                normalX = -1.0f;
                bounced = true;
            }
            if (y < radius)
            {
                y = 2.0f * radius - y;
                vy = math.abs(vy);
                normalY = 1.0f;
                bounced = true;
            }
            else if (y > @params.fieldHeight - radius)
            {
                y = 2.0f * (@params.fieldHeight - radius) - y;
                vy = -math.abs(vy);
                normalY = -1.0f;
                bounced = true;
            }

            int column = (int)math.floor(x);
            int row = (int)math.floor(y);
            if (!board.isNearBrick(paddedCellIndex(column, row, @params.paddedWidth)))
            {
                if (bounced)
                {
                    bounces += 1;
                    if (bounces >= @params.retargetBounceThreshold)
                    {
                        retargetTowardNearestBrick(x, y, ref vx, ref vy, color, normalX, normalY, board, @params);
                    }
                }
                return noBrickHit;
            }
            float fractionX = x - (float)column;
            float fractionY = y - (float)row;
            int neighborColumn = fractionX < radius ? -1 : (fractionX > 1.0f - radius ? 1 : 0);
            int neighborRow = fractionY < radius ? -1 : (fractionY > 1.0f - radius ? 1 : 0);

            float bestDistanceSquared = radius * radius;
            float bestOffsetX = 0.0f;
            float bestOffsetY = 0.0f;
            int bestColumn = 0;
            int bestRow = 0;
            int bestBrick = -1;
            int bestColor = -1;
            for (int candidate = 0; candidate < 4; ++candidate)
            {
                int cellColumn = column + ((candidate & 1) != 0 ? neighborColumn : 0);
                int cellRow = row + ((candidate & 2) != 0 ? neighborRow : 0);
                int cellColor;
                int brick = board.occupant(paddedCellIndex(cellColumn, cellRow, @params.paddedWidth), out cellColor);
                if (brick < 0)
                {
                    continue;
                }
                float closestX = clampFloat(x, (float)cellColumn, (float)(cellColumn + 1));
                float closestY = clampFloat(y, (float)cellRow, (float)(cellRow + 1));
                float offsetX = x - closestX;
                float offsetY = y - closestY;
                float distanceSquared = offsetX * offsetX + offsetY * offsetY;
                if (distanceSquared < bestDistanceSquared)
                {
                    bestDistanceSquared = distanceSquared;
                    bestOffsetX = offsetX;
                    bestOffsetY = offsetY;
                    bestColumn = cellColumn;
                    bestRow = cellRow;
                    bestBrick = brick;
                    bestColor = cellColor;
                }
            }

            if (bestBrick >= 0)
            {
                if (bestColor == color)
                {
                    return bestBrick;
                }
                float distance = math.sqrt(bestDistanceSquared);
                if (distance > 1e-6f)
                {
                    normalX = bestOffsetX / distance;
                    normalY = bestOffsetY / distance;
                    x += normalX * (radius - distance);
                    y += normalY * (radius - distance);
                }
                else
                {
                    pushOutOfCell(ref x, ref y, out normalX, out normalY, bestColumn, bestRow, radius);
                }
                reflectVelocity(ref vx, ref vy, normalX, normalY);
                bounced = true;
            }

            if (bounced)
            {
                bounces += 1;
                if (bounces >= @params.retargetBounceThreshold)
                {
                    retargetTowardNearestBrick(x, y, ref vx, ref vy, color, normalX, normalY, board, @params);
                }
            }
            return noBrickHit;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int stepBallSubsteps(
            ref float x,
            ref float y,
            ref float vx,
            ref float vy,
            int color,
            ref int bounces,
            in BoardView board,
            in StepParams @params)
        {
            for (int substep = 0; substep < @params.substeps; ++substep)
            {
                int brick = stepBall(ref x, ref y, ref vx, ref vy, color, ref bounces, board, @params);
                if (brick != noBrickHit)
                {
                    return brick;
                }
            }
            return noBrickHit;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float freeFlightSpan(float extent, float radius)
        {
            float span = extent - 2.0f * radius;
            return span > 1e-6f ? span : 1e-6f;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint freeFlightPhase(float position, float velocity, float extent, float radius)
        {
            float span = freeFlightSpan(extent, radius);
            float along = clampFloat(position - radius, 0.0f, span);
            float unfolded = velocity < 0.0f ? 2.0f * span - along : along;
            float fraction = unfolded / (2.0f * span);
            float wrapped = fraction >= 1.0f ? 0.0f : fraction;
            return (uint)(wrapped * freeFlightPhaseUnits);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint freeFlightAdvance(float velocity, float deltaTime, float extent, float radius)
        {
            float perTick = math.abs(velocity) * deltaTime / (2.0f * freeFlightSpan(extent, radius));
            return (uint)(perTick * freeFlightPhaseUnits + 0.5f);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float foldFreeFlightPhase(uint phase, float extent, float radius, out float direction)
        {
            const float phaseToFraction = 1.0f / 16777216.0f;
            float span = freeFlightSpan(extent, radius);
            float unfolded = (float)(phase >> 8) * phaseToFraction * 2.0f * span;
            bool outbound = phase < freeFlightPhaseHalf;
            direction = outbound ? 1.0f : -1.0f;
            return radius + (outbound ? unfolded : 2.0f * span - unfolded);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint freeFlightWallCrossings(uint startPhase, uint advance, uint ticks)
        {
            ulong travelled = (ulong)startPhase + (ulong)advance * (ulong)ticks;
            return (uint)((travelled >> 31) - ((ulong)startPhase >> 31));
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static FreeFlightPosition flyFreely(
            float x,
            float y,
            float vx,
            float vy,
            uint ticks,
            in StepParams @params)
        {
            uint phaseX = freeFlightPhase(x, vx, @params.fieldWidth, @params.radius);
            uint phaseY = freeFlightPhase(y, vy, @params.fieldHeight, @params.radius);
            uint advanceX = freeFlightAdvance(vx, @params.deltaTime, @params.fieldWidth, @params.radius);
            uint advanceY = freeFlightAdvance(vy, @params.deltaTime, @params.fieldHeight, @params.radius);
            FreeFlightPosition flown = default;
            float directionX;
            float directionY;
            flown.x = foldFreeFlightPhase(phaseX + advanceX * ticks, @params.fieldWidth, @params.radius, out directionX);
            flown.y = foldFreeFlightPhase(phaseY + advanceY * ticks, @params.fieldHeight, @params.radius, out directionY);
            flown.vx = math.abs(vx) * directionX;
            flown.vy = math.abs(vy) * directionY;
            flown.wallCrossings = freeFlightWallCrossings(phaseX, advanceX, ticks) +
                                  freeFlightWallCrossings(phaseY, advanceY, ticks);
            return flown;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static FreeFlightPosition flyFreelyBackwards(
            float x,
            float y,
            float vx,
            float vy,
            uint ticks,
            in StepParams @params)
        {
            FreeFlightPosition reversed = flyFreely(x, y, -vx, -vy, ticks, @params);
            reversed.vx = -reversed.vx;
            reversed.vy = -reversed.vy;
            return reversed;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint PackRenderCoordinate(float value, float scale) => packRenderCoordinate(value, scale);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint PackBallForRender(float x, float y, int color, float scaleX, float scaleY) => packBallForRender(x, y, color, scaleX, scaleY);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float ClampFloat(float value, float low, float high) => clampFloat(value, low, high);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int PaddedCellIndex(int column, int row, int paddedWidth) => paddedCellIndex(column, row, paddedWidth);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void PushOutOfCell(ref float x, ref float y, out float normalX, out float normalY, int column, int row, float radius) =>
            pushOutOfCell(ref x, ref y, out normalX, out normalY, column, row, radius);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void RetargetTowardNearestBrick(float x, float y, ref float vx, ref float vy, int color, float normalX, float normalY, in BoardView board, in StepParams @params) =>
            retargetTowardNearestBrick(x, y, ref vx, ref vy, color, normalX, normalY, board, @params);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void ReflectVelocity(ref float vx, ref float vy, float normalX, float normalY) =>
            reflectVelocity(ref vx, ref vy, normalX, normalY);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void ResolveEmbeddedContact(ref float x, ref float y, ref float vx, ref float vy, int color, ref int bounces, int column, int row, in BoardView board, in StepParams @params) =>
            resolveEmbeddedContact(ref x, ref y, ref vx, ref vy, color, ref bounces, column, row, board, @params);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int StepBall(ref float x, ref float y, ref float vx, ref float vy, int color, ref int bounces, in BoardView board, in StepParams @params) =>
            stepBall(ref x, ref y, ref vx, ref vy, color, ref bounces, board, @params);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int StepBallSubsteps(ref float x, ref float y, ref float vx, ref float vy, int color, ref int bounces, in BoardView board, in StepParams @params) =>
            stepBallSubsteps(ref x, ref y, ref vx, ref vy, color, ref bounces, board, @params);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float FreeFlightSpan(float extent, float radius) => freeFlightSpan(extent, radius);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint FreeFlightPhase(float position, float velocity, float extent, float radius) => freeFlightPhase(position, velocity, extent, radius);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint FreeFlightAdvance(float velocity, float deltaTime, float extent, float radius) => freeFlightAdvance(velocity, deltaTime, extent, radius);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float FoldFreeFlightPhase(uint phase, float extent, float radius, out float direction) => foldFreeFlightPhase(phase, extent, radius, out direction);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static uint FreeFlightWallCrossings(uint startPhase, uint advance, uint ticks) => freeFlightWallCrossings(startPhase, advance, ticks);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static FreeFlightPosition FlyFreely(float x, float y, float vx, float vy, uint ticks, in StepParams @params) =>
            flyFreely(x, y, vx, vy, ticks, @params);

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static FreeFlightPosition FlyFreelyBackwards(float x, float y, float vx, float vy, uint ticks, in StepParams @params) =>
            flyFreelyBackwards(x, y, vx, vy, ticks, @params);
    }
}
