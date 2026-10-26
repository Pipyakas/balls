#include "BallStepCore.h"

struct DeviceBalls {
    unsigned int* position;
    unsigned int* state;
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

inline constexpr float positionScale = 65535.0f;
inline constexpr unsigned int positionMask = 0xFFFFu;
inline constexpr int directionBits = 13;
inline constexpr unsigned int directionMask = (1u << directionBits) - 1u;
inline constexpr float directionScale = 4095.0f;
inline constexpr unsigned int colorShift = 26u;
inline constexpr unsigned int colorMask = 7u;
inline constexpr unsigned int bounceShift = 29u;
inline constexpr unsigned int bounceMask = 3u;
inline constexpr unsigned int deadColor = 7u;
inline constexpr int savedBounceLimit = 3;

__device__ __forceinline__ unsigned int stateColor(unsigned int state) { return (state >> colorShift) & colorMask; }

__device__ __forceinline__ unsigned int encodeAxis(float value, float extent) {
    const float scaled = value / extent * positionScale + 0.5f;
    return static_cast<unsigned int>(fminf(fmaxf(scaled, 0.0f), positionScale));
}

__device__ __forceinline__ unsigned int encodePosition(float x, float y, const StepParams& params) {
    return encodeAxis(x, params.fieldWidth) | (encodeAxis(y, params.fieldHeight) << 16);
}

__device__ __forceinline__ void decodePosition(unsigned int position, float& x, float& y, const StepParams& params) {
    x = static_cast<float>(position & positionMask) * (params.fieldWidth / positionScale);
    y = static_cast<float>(position >> 16) * (params.fieldHeight / positionScale);
}

__device__ __forceinline__ int signedComponent(unsigned int bits) {
    return static_cast<int>(bits << (32 - directionBits)) >> (32 - directionBits);
}

__device__ __forceinline__ unsigned int encodeComponent(float value) {
    const int quantized = __float2int_rn(fminf(fmaxf(value, -1.0f), 1.0f) * directionScale);
    return static_cast<unsigned int>(quantized) & directionMask;
}

__device__ __forceinline__ unsigned int encodeState(float vx, float vy, unsigned int color, int bounces,
                                                    const StepParams& params) {
    const float inverseSpeed = 1.0f / params.speed;
    const unsigned int savedBounces = static_cast<unsigned int>(min(bounces, savedBounceLimit));
    return encodeComponent(vx * inverseSpeed) | (encodeComponent(vy * inverseSpeed) << directionBits) |
           (color << colorShift) | (savedBounces << bounceShift);
}

__device__ __forceinline__ void decodeVelocity(unsigned int state, float& vx, float& vy, const StepParams& params) {
    const float dx = static_cast<float>(signedComponent(state & directionMask));
    const float dy = static_cast<float>(signedComponent((state >> directionBits) & directionMask));
    const float scale = params.speed * rsqrtf(fmaxf(dx * dx + dy * dy, 1.0f));
    vx = dx * scale;
    vy = dy * scale;
}

__device__ __forceinline__ unsigned int packFromPosition(unsigned int position, unsigned int color) {
    return ((position & positionMask) >> 2) | (((position >> 16) >> 2) << renderCoordinateBits) |
           (color << renderColorShift);
}

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
                                           unsigned int killedCapacity, FrameCounters* counters,
                                           VisibilityGrid grid) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) {
        return;
    }
    const unsigned int startState = balls.state[index];
    const unsigned int color = stateColor(startState);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(balls.position[index], x, y, params);
    decodeVelocity(startState, vx, vy, params);
    const int startBounces = static_cast<int>((startState >> bounceShift) & bounceMask);
    int bounces = startBounces;
    const int brick = stepBallSubsteps(x, y, vx, vy, static_cast<int>(color), bounces, board, params);
    const unsigned int position = encodePosition(x, y, params);
    balls.position[index] = position;
    if (bounces != startBounces) {
        balls.state[index] = encodeState(vx, vy, color, bounces, params);
    }
    const unsigned int packed = packFromPosition(position, color);
    if (grid.slots) {
        bidForSlot(packed, static_cast<unsigned int>(index), grid);
    } else {
        balls.packed[index] = packed;
    }
    if (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick])) {
        const unsigned int slot = atomicAdd(&counters->killedCount, 1u);
        if (slot < killedCapacity) {
            balls.state[index] = deadColor << colorShift;
            killedIndices[slot] = static_cast<unsigned int>(index);
            atomicAdd(&counters->killsByColor[color], 1u);
        } else {
            atomicAdd(&brickHitsLeft[brick], 1);
        }
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
    if (source >= oldCount || stateColor(balls.state[source]) == deadColor) {
        return;
    }
    const unsigned int target = holes[atomicAdd(&counters->holeCursor, 1u)];
    balls.position[target] = balls.position[source];
    balls.state[target] = balls.state[source];
    balls.packed[target] = balls.packed[source];
}
