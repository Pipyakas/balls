#include "BallStepCore.h"

struct DeviceBallHit {
    unsigned int ball;
    int brick;
};

struct DeviceBalls {
    float* x;
    float* y;
    float* vx;
    float* vy;
    int* color;
    int* bounces;
    unsigned int* packed;
};

extern "C" __global__ void stepBallsKernel(DeviceBalls balls, int count, BoardView board, StepParams params,
                                           DeviceBallHit* hits, unsigned int* hitCount, unsigned int hitCapacity) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) {
        return;
    }
    float x = balls.x[index];
    float y = balls.y[index];
    float vx = balls.vx[index];
    float vy = balls.vy[index];
    int bounces = balls.bounces[index];
    const int brick = stepBallSubsteps(x, y, vx, vy, balls.color[index], bounces, board, params);
    balls.x[index] = x;
    balls.y[index] = y;
    balls.vx[index] = vx;
    balls.vy[index] = vy;
    balls.bounces[index] = bounces;
    balls.packed[index] = packBallForRender(x, y, balls.color[index], params.packScaleX, params.packScaleY);
    if (brick != noBrickHit) {
        const unsigned int slot = atomicAdd(hitCount, 1u);
        if (slot < hitCapacity) {
            hits[slot].ball = static_cast<unsigned int>(index);
            hits[slot].brick = brick;
        }
    }
}

extern "C" __global__ void removeBallsKernel(DeviceBalls balls, int count, const unsigned int* descendingIndices,
                                             int removeCount) {
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }
    int last = count;
    for (int i = 0; i < removeCount; ++i) {
        const unsigned int target = descendingIndices[i];
        --last;
        balls.x[target] = balls.x[last];
        balls.y[target] = balls.y[last];
        balls.vx[target] = balls.vx[last];
        balls.vy[target] = balls.vy[last];
        balls.color[target] = balls.color[last];
        balls.bounces[target] = balls.bounces[last];
        balls.packed[target] = balls.packed[last];
    }
}
