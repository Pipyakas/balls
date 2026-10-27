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
    unsigned int bidderMask;
};

struct CellBoard {
    const int* cellWords;
    const int* targetCells;

    __device__ __forceinline__ bool isNearBrick(int index) const { return cellWords[index] != emptyFarCell; }

    __device__ __forceinline__ int occupant(int index, int& color) const {
        const int word = cellWords[index];
        color = word < 0 ? -1 : (word & cellColorMask);
        return word < 0 ? -1 : (word >> cellBrickShift);
    }

    __device__ __forceinline__ int targetCell(int color, int index, int cellCount) const {
        return targetCells[color * cellCount + index];
    }

    static constexpr int emptyFarCell = -1;
    static constexpr int cellColorMask = 7;
    static constexpr int cellBrickShift = 3;
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
    const unsigned int priority = slotPriority(index, grid.frameSeed);
    if ((priority >> 1) & grid.bidderMask) {
        return;
    }
    const int slot = slotForPacked(packed, grid);
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

extern "C" __global__ void gridToRenderKernel(const unsigned long long* slots, int cells, unsigned int* renderSlots) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= cells) {
        return;
    }
    const unsigned long long key = slots[index];
    renderSlots[index] = key == 0ull ? emptyRenderSlot : static_cast<unsigned int>(key);
}

extern "C" __global__ void applyCellWordsKernel(const int* updates, int updateCount, int* cellWords) {
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < updateCount) {
        cellWords[updates[2 * index]] = updates[2 * index + 1];
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
                                           const unsigned int* holes, FrameCounters* counters,
                                           unsigned int packedCapacity) {
    const unsigned int source = survivorCount + blockIdx.x * blockDim.x + threadIdx.x;
    if (source >= oldCount || stateColor(balls.state[source]) == deadColor) {
        return;
    }
    const unsigned int target = holes[atomicAdd(&counters->holeCursor, 1u)];
    balls.position[target] = balls.position[source];
    balls.state[target] = balls.state[source];
    if (source < packedCapacity) {
        balls.packed[target] = balls.packed[source];
    }
}

inline constexpr int maxSortCells = 4096;
inline constexpr unsigned int fullSortMask = 0xFFFFFFFFu;
inline constexpr int sortItemsPerThread = 16;
inline constexpr int sortThreadsPerBlock = 256;
inline constexpr int sortBallsPerBlock = sortItemsPerThread * sortThreadsPerBlock;

__device__ __forceinline__ int sortCellOf(unsigned int position, int columns, int rows) {
    const int column =
        min(static_cast<int>(((position & positionMask) * static_cast<unsigned int>(columns)) >> 16), columns - 1);
    const int row = min(static_cast<int>(((position >> 16) * static_cast<unsigned int>(rows)) >> 16), rows - 1);
    return row * columns + column;
}

#if defined(BALLS_RANK_WITH_ATOMICS)
__device__ __forceinline__ unsigned int rankInCell(unsigned int* cellTotals, int cell, bool valid) {
    return valid ? atomicAdd(&cellTotals[cell], 1u) : 0u;
}
#else
__device__ __forceinline__ unsigned int rankInCell(unsigned int* cellTotals, int cell, bool valid) {
    const unsigned int voters = __ballot_sync(fullSortMask, valid);
    if (!valid) {
        return 0u;
    }
    const unsigned int peers = __match_any_sync(voters, cell);
    const int lane = static_cast<int>(threadIdx.x) & 31;
    const int leader = __ffs(peers) - 1;
    unsigned int base = 0u;
    if (lane == leader) {
        base = atomicAdd(&cellTotals[cell], static_cast<unsigned int>(__popc(peers)));
    }
    base = __shfl_sync(peers, base, leader);
    return base + static_cast<unsigned int>(__popc(peers & ((1u << lane) - 1u)));
}
#endif

__device__ __forceinline__ void clearSharedCells(unsigned int* cells, int cellCount) {
    for (int cell = threadIdx.x; cell < cellCount; cell += blockDim.x) {
        cells[cell] = 0u;
    }
}

extern "C" __global__ void countCellsKernel(const unsigned int* position, int count, int columns, int rows,
                                            unsigned int* cellCounts) {
    __shared__ unsigned int local[maxSortCells];
    const int cellCount = columns * rows;
    clearSharedCells(local, cellCount);
    __syncthreads();
    const int blockStart = blockIdx.x * sortBallsPerBlock;
    for (int item = 0; item < sortItemsPerThread; ++item) {
        const int index = blockStart + item * sortThreadsPerBlock + threadIdx.x;
        const bool valid = index < count;
        rankInCell(local, valid ? sortCellOf(position[index], columns, rows) : 0, valid);
    }
    __syncthreads();
    for (int cell = threadIdx.x; cell < cellCount; cell += blockDim.x) {
        if (local[cell] != 0u) {
            atomicAdd(&cellCounts[cell], local[cell]);
        }
    }
}

extern "C" __global__ void scanCellsKernel(unsigned int* cellCounts, int cellCount) {
    __shared__ unsigned int local[maxSortCells];
    for (int cell = threadIdx.x; cell < cellCount; cell += blockDim.x) {
        local[cell] = cellCounts[cell];
    }
    __syncthreads();
    if (threadIdx.x == 0) {
        unsigned int running = 0u;
        for (int cell = 0; cell < cellCount; ++cell) {
            const unsigned int cellTotal = local[cell];
            local[cell] = running;
            running += cellTotal;
        }
    }
    __syncthreads();
    for (int cell = threadIdx.x; cell < cellCount; cell += blockDim.x) {
        cellCounts[cell] = local[cell];
    }
}


inline constexpr int stepSortThreads = 512;
inline constexpr int stepSortItemsPerThread = 8;
inline constexpr int stepSortBallsPerBlock = stepSortThreads * stepSortItemsPerThread;
inline constexpr unsigned int rankBits = 12u;
inline constexpr unsigned int rankMask = (1u << rankBits) - 1u;

inline constexpr int ballsPerStepThread = 2;

struct SortedOutput {
    unsigned int* cellCursors;
    unsigned int* position;
    unsigned int* state;
    int columns;
    int rows;
};

__device__ __forceinline__ void stepOneBall(unsigned int target, unsigned int startPosition, unsigned int startState,
                                            unsigned int* outPosition, unsigned int* outState, bool writeAllState,
                                            unsigned int* packedOut, const CellBoard& board, const StepParams& params,
                                            int* brickHitsLeft, unsigned int* killedIndices,
                                            unsigned int killedCapacity, FrameCounters* counters,
                                            const VisibilityGrid& grid, unsigned int packedCapacity) {
    const unsigned int color = stateColor(startState);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(startPosition, x, y, params);
    decodeVelocity(startState, vx, vy, params);
    const int startBounces = static_cast<int>((startState >> bounceShift) & bounceMask);
    int bounces = startBounces;
    const int brick = stepBallSubsteps(x, y, vx, vy, static_cast<int>(color), bounces, board, params);
    const unsigned int position = encodePosition(x, y, params);
    outPosition[target] = position;
    if (bounces != startBounces) {
        outState[target] = encodeState(vx, vy, color, bounces, params);
    } else if (writeAllState) {
        outState[target] = startState;
    }
    const unsigned int packed = packFromPosition(position, color);
    if (grid.slots) {
        bidForSlot(packed, target, grid);
    } else if (target < packedCapacity) {
        packedOut[target] = packed;
    }
    if (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick])) {
        const unsigned int slot = atomicAdd(&counters->killedCount, 1u);
        if (slot < killedCapacity) {
            outState[target] = deadColor << colorShift;
            killedIndices[slot] = target;
            killedIndices[killedCapacity + slot] = static_cast<unsigned int>(brick);
            atomicAdd(&counters->killsByColor[color], 1u);
        } else {
            atomicAdd(&brickHitsLeft[brick], 1);
        }
    }
}

extern "C" __global__ void stepBallsKernel(DeviceBalls balls, int count, const int* cellWords, const int* targetCells,
                                           StepParams params, int* brickHitsLeft, unsigned int* killedIndices,
                                           unsigned int killedCapacity, FrameCounters* counters, VisibilityGrid grid,
                                           unsigned int packedCapacity) {
    const int blockStart = static_cast<int>(blockIdx.x) * ballsPerStepThread * static_cast<int>(blockDim.x);
    unsigned int positions[ballsPerStepThread];
    unsigned int states[ballsPerStepThread];
    for (int item = 0; item < ballsPerStepThread; ++item) {
        const int index = blockStart + item * static_cast<int>(blockDim.x) + static_cast<int>(threadIdx.x);
        positions[item] = index < count ? balls.position[index] : 0u;
        states[item] = index < count ? balls.state[index] : 0u;
    }
    const CellBoard board{cellWords, targetCells};
    for (int item = 0; item < ballsPerStepThread; ++item) {
        const int index = blockStart + item * static_cast<int>(blockDim.x) + static_cast<int>(threadIdx.x);
        if (index >= count) {
            return;
        }
        stepOneBall(static_cast<unsigned int>(index), positions[item], states[item], balls.position, balls.state,
                    false, balls.packed, board, params, brickHitsLeft, killedIndices, killedCapacity, counters, grid,
                    packedCapacity);
    }
}

extern "C" __global__ void __launch_bounds__(stepSortThreads, 2)
    stepAndSortBallsKernel(DeviceBalls balls, int count, const int* cellWords, const int* targetCells,
                           StepParams params, int* brickHitsLeft, unsigned int* killedIndices,
                           unsigned int killedCapacity, FrameCounters* counters, VisibilityGrid grid,
                           unsigned int packedCapacity, SortedOutput sorted) {
    __shared__ unsigned int blockCells[maxSortCells];
    __shared__ unsigned int cellAndRank[stepSortBallsPerBlock];
    const int cellCount = sorted.columns * sorted.rows;
    clearSharedCells(blockCells, cellCount);
    __syncthreads();
    const int blockStart = blockIdx.x * stepSortBallsPerBlock;
    for (int item = 0; item < stepSortItemsPerThread; ++item) {
        const int slot = item * stepSortThreads + threadIdx.x;
        const int index = blockStart + slot;
        const bool valid = index < count;
        const int cell = valid ? sortCellOf(balls.position[index], sorted.columns, sorted.rows) : 0;
        const unsigned int rank = rankInCell(blockCells, cell, valid);
        cellAndRank[slot] = (static_cast<unsigned int>(cell) << rankBits) | rank;
    }
    __syncthreads();
    for (int cell = threadIdx.x; cell < cellCount; cell += blockDim.x) {
        if (blockCells[cell] != 0u) {
            blockCells[cell] = atomicAdd(&sorted.cellCursors[cell], blockCells[cell]);
        }
    }
    __syncthreads();
    const CellBoard board{cellWords, targetCells};
    for (int item = 0; item < stepSortItemsPerThread; ++item) {
        const int slot = item * stepSortThreads + threadIdx.x;
        const int index = blockStart + slot;
        if (index >= count) {
            break;
        }
        const unsigned int packedSlot = cellAndRank[slot];
        const unsigned int target = blockCells[packedSlot >> rankBits] + (packedSlot & rankMask);
        stepOneBall(target, balls.position[index], balls.state[index], sorted.position, sorted.state, true,
                    balls.packed, board, params, brickHitsLeft, killedIndices, killedCapacity, counters, grid,
                    packedCapacity);
    }
}

// While every ball flies freely the stored state is each ball at the epoch tick. Rendering a frame only needs the
// balls that bid for a visibility slot, so whole 256-ball chunks are sampled with a stride: the reads stay
// contiguous and the cost follows the number of slots, not the number of balls.
extern "C" __global__ void freeFlightRenderKernel(DeviceBalls balls, int count, StepParams params,
                                                  unsigned int ticksSinceEpoch, VisibilityGrid grid,
                                                  unsigned int packedCapacity, unsigned int chunkStride,
                                                  unsigned int chunkOffset) {
    const unsigned long long chunk =
        static_cast<unsigned long long>(chunkOffset) + static_cast<unsigned long long>(blockIdx.x) * chunkStride;
    const unsigned long long wide = chunk * blockDim.x + threadIdx.x;
    if (wide >= static_cast<unsigned long long>(count)) {
        return;
    }
    const unsigned int index = static_cast<unsigned int>(wide);
    const unsigned int state = balls.state[index];
    const unsigned int color = stateColor(state);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(balls.position[index], x, y, params);
    decodeVelocity(state, vx, vy, params);
    const FreeFlightPosition flown = flyFreely(x, y, vx, vy, ticksSinceEpoch, params);
    const unsigned int packed = packBallForRender(flown.x, flown.y, static_cast<int>(color), params.packScaleX,
                                                  params.packScaleY);
    if (grid.slots) {
        bidForSlot(packed, index, grid);
    } else if (index < packedCapacity) {
        balls.packed[index] = packed;
    }
}

extern "C" __global__ void materializeFreeFlightKernel(DeviceBalls balls, int count, StepParams params,
                                                       unsigned int ticksSinceEpoch) {
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= count) {
        return;
    }
    const unsigned int state = balls.state[index];
    const unsigned int color = stateColor(state);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(balls.position[index], x, y, params);
    decodeVelocity(state, vx, vy, params);
    const FreeFlightPosition flown = flyFreely(x, y, vx, vy, ticksSinceEpoch, params);
    const int bounces = static_cast<int>((state >> bounceShift) & bounceMask) +
                        static_cast<int>(min(flown.wallCrossings, static_cast<unsigned int>(savedBounceLimit)));
    balls.position[index] = encodePosition(flown.x, flown.y, params);
    balls.state[index] = encodeState(flown.vx, flown.vy, color, bounces, params);
}
