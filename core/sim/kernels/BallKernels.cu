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
                                           FrameCounters* counters) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) {
        return;
    }
    float x = balls.x[index];
    float y = balls.y[index];
    float vx = balls.vx[index];
    float vy = balls.vy[index];
    int bounces = balls.bounces[index];
    const int color = balls.color[index];
    const int brick = stepBallSubsteps(x, y, vx, vy, color, bounces, board, params);
    balls.x[index] = x;
    balls.y[index] = y;
    balls.vx[index] = vx;
    balls.vy[index] = vy;
    balls.bounces[index] = bounces;
    balls.packed[index] = packBallForRender(x, y, color, params.packScaleX, params.packScaleY);
    if (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick])) {
        balls.color[index] = -1;
        killedIndices[atomicAdd(&counters->killedCount, 1u)] = static_cast<unsigned int>(index);
        atomicAdd(&counters->killsByColor[color], 1u);
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
