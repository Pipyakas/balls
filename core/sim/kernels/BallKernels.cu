#include "BallStepCore.h"

struct DeviceBalls {
    float* x;
    float* y;
    float* vx;
    float* vy;
    int* color;
    int* bounces;
    unsigned int* packed;
};

struct VisibilityGrid {
    unsigned int* slots;
    unsigned int* priorities;
    int width;
    int height;
    float scaleX;
    float scaleY;
    unsigned int frameSeed;
};

__device__ __forceinline__ unsigned int slotPriority(unsigned int index, unsigned int frameSeed) {
    unsigned int hash = index ^ frameSeed;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash | 1u;
}

__device__ __forceinline__ int slotForPacked(unsigned int packed, const VisibilityGrid& grid) {
    const float cellsPerUnitX = grid.scaleX;
    const float cellsPerUnitY = grid.scaleY;
    const int column = min(static_cast<int>(static_cast<float>(packed & renderCoordinateMax) * cellsPerUnitX), grid.width - 1);
    const int row = min(static_cast<int>(static_cast<float>((packed >> renderCoordinateBits) & renderCoordinateMax) * cellsPerUnitY),
                        grid.height - 1);
    return row * grid.width + column;
}

struct FrameCounters {
    unsigned int killedCount;
    unsigned int holeCount;
    unsigned int holeCursor;
    unsigned int killsByColor[7];
};

__device__ __forceinline__ bool tryConsumeHit(int* hitsLeft) {
    int current = *hitsLeft;
    while (current > 0) {
        const int previous = atomicCAS(hitsLeft, current, current - 1);
        if (previous == current) {
            return true;
        }
        current = previous;
    }
    return false;
}

extern "C" __global__ void stepBallsKernel(DeviceBalls balls, int count, BoardView board, StepParams params,
                                           int* brickHitsLeft, unsigned int* killedIndices,
                                           FrameCounters* counters, VisibilityGrid grid) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) {
        return;
    }
    float x = balls.x[index];
    float y = balls.y[index];
    const float startVx = balls.vx[index];
    const float startVy = balls.vy[index];
    const int startBounces = balls.bounces[index];
    float vx = startVx;
    float vy = startVy;
    int bounces = startBounces;
    const int color = balls.color[index];
    const int brick = stepBallSubsteps(x, y, vx, vy, color, bounces, board, params);
    balls.x[index] = x;
    balls.y[index] = y;
    if (bounces != startBounces) {
        balls.vx[index] = vx;
        balls.vy[index] = vy;
        balls.bounces[index] = bounces;
    }
    const unsigned int packed = packBallForRender(x, y, color, params.packScaleX, params.packScaleY);
    balls.packed[index] = packed;
    if (grid.slots) {
        const int slot = slotForPacked(packed, grid);
        const unsigned int priority = slotPriority(static_cast<unsigned int>(index), grid.frameSeed);
        if (priority > grid.priorities[slot]) {
            atomicMax(&grid.priorities[slot], priority);
        }
    }
    if (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick])) {
        balls.color[index] = -1;
        killedIndices[atomicAdd(&counters->killedCount, 1u)] = static_cast<unsigned int>(index);
        atomicAdd(&counters->killsByColor[color], 1u);
    }
}

extern "C" __global__ void resolveVisibilityKernel(const unsigned int* packedBalls, int count, VisibilityGrid grid) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) {
        return;
    }
    const unsigned int packed = packedBalls[index];
    const int slot = slotForPacked(packed, grid);
    if (grid.priorities[slot] == slotPriority(static_cast<unsigned int>(index), grid.frameSeed)) {
        grid.slots[slot] = packed;
    }
}

extern "C" __global__ void collectHolesKernel(const unsigned int* killedIndices, unsigned int killedCount,
                                              unsigned int survivorCount, unsigned int* holes,
                                              FrameCounters* counters) {
    const unsigned int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= killedCount) {
        return;
    }
    const unsigned int killed = killedIndices[index];
    if (killed < survivorCount) {
        holes[atomicAdd(&counters->holeCount, 1u)] = killed;
    }
}

extern "C" __global__ void fillHolesKernel(DeviceBalls balls, unsigned int survivorCount, unsigned int oldCount,
                                           const unsigned int* holes, FrameCounters* counters) {
    const unsigned int source = survivorCount + blockIdx.x * blockDim.x + threadIdx.x;
    if (source >= oldCount || balls.color[source] < 0) {
        return;
    }
    const unsigned int target = holes[atomicAdd(&counters->holeCursor, 1u)];
    balls.x[target] = balls.x[source];
    balls.y[target] = balls.y[source];
    balls.vx[target] = balls.vx[source];
    balls.vy[target] = balls.vy[source];
    balls.color[target] = balls.color[source];
    balls.bounces[target] = balls.bounces[source];
    balls.packed[target] = balls.packed[source];
}
