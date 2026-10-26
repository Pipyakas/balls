#include "BallStepCore.h"

struct DeviceBalls {
    float* x;
    float* y;
    float* vx;
    float* vy;
    unsigned int* meta;
    unsigned int* packed;
};

struct VisibilityGrid {
    unsigned long long* slots;
    int width;
    int height;
    float scaleX;
    float scaleY;
    unsigned int frameSeed;
};

struct FrameCounters {
    unsigned int killedCount;
    unsigned int holeCount;
    unsigned int holeCursor;
    unsigned int killsByColor[7];
};

inline constexpr unsigned int metaColorMask = 7u;
inline constexpr unsigned int metaBounceShift = 3u;
inline constexpr unsigned int deadColor = 7u;

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
    const int column =
        min(static_cast<int>(static_cast<float>(packed & renderCoordinateMax) * grid.scaleX), grid.width - 1);
    const int row = min(
        static_cast<int>(static_cast<float>((packed >> renderCoordinateBits) & renderCoordinateMax) * grid.scaleY),
        grid.height - 1);
    return row * grid.width + column;
}

__device__ __forceinline__ void bidForSlot(unsigned int packed, unsigned int index, const VisibilityGrid& grid) {
    const int slot = slotForPacked(packed, grid);
    const unsigned int priority = slotPriority(index, grid.frameSeed);
    if (priority > static_cast<unsigned int>(grid.slots[slot] >> 32)) {
        atomicMax(&grid.slots[slot], (static_cast<unsigned long long>(priority) << 32) | packed);
    }
}

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
    const unsigned int meta = balls.meta[index];
    const int color = static_cast<int>(meta & metaColorMask);
    const int startBounces = static_cast<int>(meta >> metaBounceShift);
    float vx = startVx;
    float vy = startVy;
    int bounces = startBounces;
    const int brick = stepBallSubsteps(x, y, vx, vy, color, bounces, board, params);
    balls.x[index] = x;
    balls.y[index] = y;
    if (bounces != startBounces) {
        balls.vx[index] = vx;
        balls.vy[index] = vy;
        balls.meta[index] = static_cast<unsigned int>(color) | (static_cast<unsigned int>(bounces) << metaBounceShift);
    }
    const unsigned int packed = packBallForRender(x, y, color, params.packScaleX, params.packScaleY);
    if (grid.slots) {
        bidForSlot(packed, static_cast<unsigned int>(index), grid);
    } else {
        balls.packed[index] = packed;
    }
    if (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick])) {
        balls.meta[index] = deadColor;
        killedIndices[atomicAdd(&counters->killedCount, 1u)] = static_cast<unsigned int>(index);
        atomicAdd(&counters->killsByColor[color], 1u);
    }
}

extern "C" __global__ void gridToRenderKernel(const unsigned long long* slots, int cells, unsigned int* renderSlots) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= cells) {
        return;
    }
    const unsigned long long key = slots[index];
    renderSlots[index] = key == 0ull ? emptyRenderSlot : static_cast<unsigned int>(key);
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
    if (source >= oldCount || (balls.meta[source] & metaColorMask) == deadColor) {
        return;
    }
    const unsigned int target = holes[atomicAdd(&counters->holeCursor, 1u)];
    balls.x[target] = balls.x[source];
    balls.y[target] = balls.y[source];
    balls.vx[target] = balls.vx[source];
    balls.vy[target] = balls.vy[source];
    balls.meta[target] = balls.meta[source];
    balls.packed[target] = balls.packed[source];
}
