#include "cuda/CudaBackend.h"

#include "common/WorkerPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>

namespace {

constexpr unsigned int threadsPerBlock = 256;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr unsigned int writeDiscardFlag = 0x02;
constexpr size_t stateArrayCount = 2;
constexpr size_t capacityGranule = size_t{1} << 20;
constexpr size_t growthDivisor = 4;
constexpr size_t growInPlaceBalls = size_t{1} << 24;
constexpr size_t heldBytesPerBall = 8;
// Kept free for the visibility grid, the render interop buffer and the driver.
constexpr size_t deviceMemoryReserve = size_t{256} << 20;
constexpr unsigned int minimumKilledCapacity = 1024;
constexpr size_t encodeChunkBalls = size_t{1} << 13;
constexpr unsigned int maximumKilledCapacity = 1u << 22;
constexpr float positionScale = 65535.0f;
constexpr unsigned int positionMask = 0xFFFFu;
constexpr int directionBits = 13;
constexpr unsigned int directionMask = (1u << directionBits) - 1u;
constexpr float directionScale = 4095.0f;
constexpr unsigned int colorShift = 26u;
constexpr unsigned int colorMask = 7u;
constexpr unsigned int deadColor = 7u;
constexpr int maxSortCells = 4096;
constexpr size_t killedRecordWords = 2;
constexpr size_t sortBallsPerBlock = 4096;
constexpr unsigned int stepSortThreads = 512;
constexpr size_t ballsPerStepThread = 2;

uint32_t encodeAxis(float value, float extent) {
    const float scaled = value / extent * positionScale + 0.5f;
    return static_cast<uint32_t>(std::clamp(scaled, 0.0f, positionScale));
}

uint32_t encodeComponent(float value) {
    const int quantized = static_cast<int>(std::lround(std::clamp(value, -1.0f, 1.0f) * directionScale));
    return static_cast<uint32_t>(quantized) & directionMask;
}

int signedComponent(uint32_t bits) {
    return static_cast<int32_t>(bits << (32 - directionBits)) >> (32 - directionBits);
}

uint32_t stateColor(uint32_t state) { return (state >> colorShift) & colorMask; }

struct DeviceSortedOutput {
    CUdeviceptr cellCursors;
    CUdeviceptr position;
    CUdeviceptr state;
    int columns;
    int rows;
};

unsigned int bidderMaskFor(size_t balls, size_t cells) {
    constexpr size_t biddersPerSlot = 32;
    unsigned int mask = 0;
    while (mask < 0x3FFFFFFFu && (static_cast<size_t>(mask) + 1) * 2 * cells * biddersPerSlot <= balls) {
        mask = mask * 2 + 1;
    }
    return mask;
}

constexpr int emptyFarCell = -1;
constexpr int emptyNearCell = -2;
constexpr int cellBrickShift = 3;

int32_t cellWordAt(const BoardUpload& board, size_t cell) {
    const int32_t brick = board.paddedCells[cell];
    if (brick >= 0) {
        return (brick << cellBrickShift) | board.brickColors[static_cast<size_t>(brick)];
    }
    return board.nearBrickCells[cell] != 0 ? emptyNearCell : emptyFarCell;
}

std::vector<int32_t> buildCellWords(const BoardUpload& board) {
    std::vector<int32_t> words(board.paddedCells.size());
    for (size_t cell = 0; cell < words.size(); ++cell) {
        words[cell] = cellWordAt(board, cell);
    }
    return words;
}

std::vector<int32_t> buildCellWordUpdates(const BoardUpload& board) {
    std::vector<int32_t> updates;
    updates.reserve(board.changedCells.size() * 2);
    for (const int32_t cell : board.changedCells) {
        updates.push_back(cell);
        updates.push_back(cellWordAt(board, static_cast<size_t>(cell)));
    }
    return updates;
}

unsigned int blocksFor(size_t threads) {
    return static_cast<unsigned int>((threads + threadsPerBlock - 1) / threadsPerBlock);
}

}

std::string CudaBackend::unavailableReason(int deviceOrdinal) {
    const CudaDriver& driver = CudaDriver::forDevice(deviceOrdinal);
    return driver.isReady() ? std::string{} : driver.error();
}

CudaBackend::CudaBackend(int deviceOrdinal) : driver(CudaDriver::forDevice(deviceOrdinal)) {
    driver.makeCurrent();
    stepFunction = driver.function("stepBallsKernel");
    collectHolesFunction = driver.function("collectHolesKernel");
    fillHolesFunction = driver.function("fillHolesKernel");
    gridToRenderFunction = driver.function("gridToRenderKernel");
    countCellsFunction = driver.function("countCellsKernel");
    scanCellsFunction = driver.function("scanCellsKernel");
    stepAndSortFunction = driver.function("stepAndSortBallsKernel");
    applyCellWordsFunction = driver.function("applyCellWordsKernel");
    freeFlightRenderFunction = driver.function("freeFlightRenderKernel");
    materializeFreeFlightFunction = driver.function("materializeFreeFlightKernel");
    if (!stepFunction || !collectHolesFunction || !fillHolesFunction || !gridToRenderFunction ||
        !countCellsFunction || !scanCellsFunction || !stepAndSortFunction || !applyCellWordsFunction ||
        !freeFlightRenderFunction || !materializeFreeFlightFunction) {
        fault = "CUDA kernels missing from module";
        return;
    }
    growInPlace = reserveGrowableArrays();
    ensureBallCapacity(initialBallCapacity);
    ensureArray(counters, sizeof(FrameCounters));
    if (driver.api().eventCreate) {
        driver.api().eventCreate(&stepStarted, CU_EVENT_DEFAULT);
        driver.api().eventCreate(&stepFinished, CU_EVENT_DEFAULT);
        driver.api().eventCreate(&stagingRead, CU_EVENT_DISABLE_TIMING);
    }
}

size_t CudaBackend::streamedBytesPerBall(bool gridActive) {
    constexpr size_t readBytes = 2 * sizeof(uint32_t);
    constexpr size_t positionWriteBytes = sizeof(uint32_t);
    constexpr size_t packedWriteBytes = sizeof(uint32_t);
    return readBytes + positionWriteBytes + (gridActive ? 0 : packedWriteBytes);
}

GpuRoofline CudaBackend::measureRoofline(size_t bufferBytes) {
    GpuRoofline roofline;
    const CudaDriver& driver = CudaDriver::instance();
    if (!driver.isReady() || !driver.api().eventCreate) {
        return roofline;
    }
    driver.makeCurrent();
    const auto& api = driver.api();
    constexpr double kilohertzToMegahertz = 1.0e-3;
    constexpr double bitsPerByte = 8.0;
    constexpr double doubleDataRate = 2.0;
    roofline.multiprocessors = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
    roofline.coreClockMegahertz = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_CLOCK_RATE) * kilohertzToMegahertz;
    roofline.memoryClockMegahertz = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE) * kilohertzToMegahertz;
    roofline.memoryBusBits = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH);
    roofline.theoreticalGigabytesPerSecond =
        doubleDataRate * roofline.memoryClockMegahertz * 1.0e6 * (roofline.memoryBusBits / bitsPerByte) / 1.0e9;

    CUdeviceptr source = 0;
    CUdeviceptr target = 0;
    CUevent started = nullptr;
    CUevent finished = nullptr;
    if (api.memAlloc(&source, bufferBytes) != CUDA_SUCCESS || api.memAlloc(&target, bufferBytes) != CUDA_SUCCESS) {
        return roofline;
    }
    api.eventCreate(&started, CU_EVENT_DEFAULT);
    api.eventCreate(&finished, CU_EVENT_DEFAULT);
    constexpr int warmupCopies = 2;
    constexpr int timedCopies = 10;
    for (int copy = 0; copy < warmupCopies; ++copy) {
        api.memcpyDeviceToDevice(target, source, bufferBytes);
    }
    api.eventRecord(started, nullptr);
    for (int copy = 0; copy < timedCopies; ++copy) {
        api.memcpyDeviceToDevice(target, source, bufferBytes);
    }
    api.eventRecord(finished, nullptr);
    api.eventSynchronize(finished);
    float elapsedMs = 0.0f;
    api.eventElapsedTime(&elapsedMs, started, finished);
    constexpr double readPlusWrite = 2.0;
    roofline.copyGigabytesPerSecond =
        readPlusWrite * static_cast<double>(bufferBytes) * timedCopies / (static_cast<double>(elapsedMs) * 1.0e6);
    api.eventDestroy(started);
    api.eventDestroy(finished);
    api.memFree(source);
    api.memFree(target);
    return roofline;
}

CudaBackend::~CudaBackend() {
    driver.makeCurrent();
    releaseRenderBuffer();
    const auto& api = driver.api();
    if (staging) {
        api.memFreeHost(staging);
    }
    for (CUevent event : {stepStarted, stepFinished, stagingRead}) {
        if (event && api.eventDestroy) {
            api.eventDestroy(event);
        }
    }
    if (growInPlace) {
        for (GrowableDeviceArray* array : {&positionArray, &stateArray, &sortedPositionArray, &sortedStateArray}) {
            array->release();
        }
        balls.position = balls.state = sortedPosition = sortedState = 0;
    }
    for (CUdeviceptr pointer :
         {balls.position, balls.state, balls.packed, targetCells.pointer, cellWords.pointer, brickHitsLeft.pointer, killedIndices.pointer, holes.pointer,
          counters.pointer, gridSlots.pointer, gridPacked.pointer, cellCursors.pointer, cellWordUpdates.pointer,
          brickHitUpdates.pointer, targetUpdates.pointer,
          sortedPosition, sortedState}) {
        if (pointer) {
            api.memFree(pointer);
        }
    }
    if (hostTargetCells) {
        api.memFreeHost(hostTargetCells);
    }
}

bool CudaBackend::succeeded(CUresult result, const char* what) {
    if (result == CUDA_SUCCESS) {
        return true;
    }
    if (fault.empty()) {
        fault = driver.describe(result, what);
    }
    return false;
}

bool CudaBackend::launch(CUfunction function, size_t threads, void** arguments, const char* what) {
    if (threads == 0) {
        return true;
    }
    return succeeded(driver.api().launchKernel(function, blocksFor(threads), 1, 1, threadsPerBlock, 1, 1, 0, nullptr,
                                               arguments, nullptr),
                     what);
}

void CudaBackend::ensureArray(DeviceArray& array, size_t bytes) {
    if (isFaulted() || array.bytes >= bytes) {
        return;
    }
    const auto& api = driver.api();
    if (array.pointer) {
        api.memFree(array.pointer);
    }
    const size_t newBytes = std::max(bytes, array.bytes * 2);
    array.pointer = 0;
    array.bytes = 0;
    if (succeeded(api.memAlloc(&array.pointer, newBytes), "cuMemAlloc")) {
        array.bytes = newBytes;
    }
}

void CudaBackend::upload(DeviceArray& array, const void* data, size_t bytes) {
    ensureArray(array, bytes);
    if (isFaulted() || bytes == 0) {
        return;
    }
    succeeded(driver.api().memcpyHostToDevice(array.pointer, data, bytes), "cuMemcpyHtoD");
}

size_t CudaBackend::ballCapacityLimit() const {
    if (isFaulted()) {
        return count;
    }
    if (fixedCapacity) {
        return capacity;
    }
    driver.makeCurrent();
    size_t freeBytes = 0;
    size_t totalBytes = 0;
    if (driver.api().memGetInfo(&freeBytes, &totalBytes) != CUDA_SUCCESS) {
        return std::numeric_limits<size_t>::max();
    }
    const size_t spare = freeBytes > deviceMemoryReserve ? freeBytes - deviceMemoryReserve : 0;
    return capacity + spare / heldBytesPerBall;
}

// 512 MB stays free for the renderer (interop buffers, framebuffers, the visibility grid), the display driver and
// the board's own arrays.
constexpr size_t preallocateGpuReserveBytes = size_t{512} << 20;
// Position and state, plus the two sort buffers the cell sort writes them into.
constexpr size_t preallocatedBytesPerBall = 16;
// The part of the reserve a board's arrays may take before the ball arrays make room for them.
constexpr size_t boardBytesInReserve = preallocateGpuReserveBytes / 4;

size_t CudaBackend::preallocatedBallsFor(size_t freeBytes) {
    const size_t usableBytes = freeBytes > preallocateGpuReserveBytes ? freeBytes - preallocateGpuReserveBytes : 0;
    return usableBytes / preallocatedBytesPerBall / capacityGranule * capacityGranule;
}

void CudaBackend::freeBallArrays() {
    const auto& api = driver.api();
    for (CUdeviceptr* buffer : {&balls.position, &balls.state, &sortedPosition, &sortedState}) {
        if (*buffer) {
            api.memFree(*buffer);
            *buffer = 0;
        }
    }
    capacity = 0;
    sortedCapacity = 0;
}

// Replaces the ball arrays with ones of exactly this many balls; with none held, nothing needs copying.
bool CudaBackend::allocateFixedBallArrays(size_t ballCapacity) {
    freeBallArrays();
    const auto& api = driver.api();
    const size_t bytes = ballCapacity * sizeof(uint32_t);
    if (api.memAlloc(&balls.position, bytes) != CUDA_SUCCESS || api.memAlloc(&balls.state, bytes) != CUDA_SUCCESS ||
        api.memAlloc(&sortedPosition, bytes) != CUDA_SUCCESS || api.memAlloc(&sortedState, bytes) != CUDA_SUCCESS) {
        freeBallArrays();
        return false;
    }
    capacity = ballCapacity;
    sortedCapacity = ballCapacity;
    return true;
}

size_t CudaBackend::preallocateBalls() {
    if (isFaulted()) {
        return 0;
    }
    driver.makeCurrent();
    if (fixedCapacity) {
        return capacity;
    }
    // Everything is freed first so the whole budget is one contiguous carve. The sort cursors (bounded by
    // maxSortCells) and the render words go in before it, so carving again for a board measures the same free
    // memory. Render words are only kept while there are fewer balls than visibility-grid cells, so sizing them
    // for a 4K grid covers any window; a bigger grid still grows them once, from the reserve.
    releaseSortBuffers();
    if (growInPlace) {
        for (GrowableDeviceArray* array : {&positionArray, &stateArray, &sortedPositionArray, &sortedStateArray}) {
            array->release();
        }
        balls.position = balls.state = sortedPosition = sortedState = 0;
        capacity = 0;
        sortedCapacity = 0;
        growInPlace = false;
    }
    freeBallArrays();
    constexpr size_t packedBallsUpFront = size_t{8} << 20;
    ensureArray(cellCursors, static_cast<size_t>(maxSortCells) * sizeof(int32_t));
    ensurePackedCapacity(packedBallsUpFront);
    size_t freeBytes = 0;
    size_t totalBytes = 0;
    const size_t targetCapacity =
        driver.api().memGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS ? preallocatedBallsFor(freeBytes) : 0;
    if (targetCapacity == 0 || !allocateFixedBallArrays(targetCapacity)) {
        ensureBallCapacity(initialBallCapacity);
        return 0;
    }
    fixedCapacity = true;
    preallocatedCapacity = targetCapacity;
    return capacity;
}

// Bytes the board's own arrays take: cells (a cell word and a target per color), bricks (hits left) and one tick's
// kill records.
size_t CudaBackend::boardBytes(const BoardUpload& board) {
    BallCount totalHits = 0;
    for (const int32_t hits : board.brickHitsLeft) {
        totalHits += static_cast<BallCount>(std::max(hits, 0));
    }
    const size_t killed = static_cast<size_t>(std::clamp<BallCount>(totalHits, minimumKilledCapacity, maximumKilledCapacity));
    return board.paddedCells.size() * (1 + ballColorCount) * sizeof(int32_t) + board.brickColors.size() * sizeof(int32_t) +
           killed * (killedRecordWords + 1) * sizeof(uint32_t);
}

void CudaBackend::uploadTargets(std::span<const int32_t> targets) {
    const size_t bytes = targets.size_bytes();
    if (hostTargetCells && hostTargetBytes >= bytes) {
        std::memcpy(hostTargetCells, targets.data(), bytes);
        return;
    }
    if (!hostTargetCells && targetCells.bytes >= bytes) {
        upload(targetCells, targets.data(), bytes);
        return;
    }
    releaseTargets();
    size_t freeBytes = 0;
    size_t totalBytes = 0;
    const bool queried = driver.api().memGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS;
    if (!queried || bytes <= freeBytes / 2) {
        upload(targetCells, targets.data(), bytes);
        return;
    }
    if (succeeded(driver.api().memAllocHost(&hostTargetCells, bytes), "cuMemAllocHost targets")) {
        hostTargetBytes = bytes;
        std::memcpy(hostTargetCells, targets.data(), bytes);
    }
}

void CudaBackend::releaseTargets() {
    releaseArray(targetCells);
    if (hostTargetCells) {
        driver.api().memFreeHost(hostTargetCells);
        hostTargetCells = nullptr;
        hostTargetBytes = 0;
    }
}

// Pinned host memory is mapped at the same address on the device (unified addressing on every 64-bit platform).
CUdeviceptr CudaBackend::targetPointer() const {
    return hostTargetCells ? reinterpret_cast<CUdeviceptr>(hostTargetCells) : targetCells.pointer;
}

void CudaBackend::releaseArray(DeviceArray& array) {
    if (array.pointer) {
        driver.api().memFree(array.pointer);
    }
    array = {};
}

// After a board's arrays are in, the ball arrays take what is left, up to the preallocated capacity. The reserve
// already holds a board of up to boardBytesInReserve, so such a board lands on the preallocated capacity exactly.
void CudaBackend::carveBallArraysAfterBoard(size_t boardArrayBytes) {
    size_t freeBytes = 0;
    size_t totalBytes = 0;
    const size_t boardInReserve = std::min(boardArrayBytes, boardBytesInReserve);
    size_t wanted = driver.api().memGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS
                        ? std::min(preallocatedCapacity, preallocatedBallsFor(freeBytes + boardInReserve))
                        : 0;
    while (wanted >= capacityGranule && !allocateFixedBallArrays(wanted)) {
        wanted = (wanted - wanted / 8) / capacityGranule * capacityGranule;
    }
    if (capacity == 0) {
        fixedCapacity = false;
        ensureBallCapacity(initialBallCapacity);
    }
}

// Running out of device memory is the expected ceiling once balls fly freely, so it leaves the backend usable
// with the capacity it already has instead of faulting.
bool CudaBackend::ensureBallCapacity(size_t required) {
    if (isFaulted()) {
        return false;
    }
    if (required <= capacity) {
        return true;
    }
    if (fixedCapacity) {
        return false;
    }
    if (growInPlace) {
        return growInPlaceTo(required);
    }
    releaseSortBuffers();
    const auto& api = driver.api();
    const auto roundUp = [](size_t balls) { return (balls + capacityGranule - 1) / capacityGranule * capacityGranule; };
    for (const size_t wanted : {roundUp(std::max(required, capacity + capacity / growthDivisor)), roundUp(required)}) {
        CUdeviceptr grownPosition = 0;
        CUdeviceptr grownState = 0;
        if (api.memAlloc(&grownPosition, wanted * 4) != CUDA_SUCCESS) {
            continue;
        }
        if (api.memAlloc(&grownState, wanted * 4) != CUDA_SUCCESS) {
            api.memFree(grownPosition);
            continue;
        }
        if (count > 0) {
            succeeded(api.memcpyDeviceToDevice(grownPosition, balls.position, count * 4), "cuMemcpyDtoD") &&
                succeeded(api.memcpyDeviceToDevice(grownState, balls.state, count * 4), "cuMemcpyDtoD");
        }
        if (balls.position) {
            api.memFree(balls.position);
        }
        if (balls.state) {
            api.memFree(balls.state);
        }
        balls.position = grownPosition;
        balls.state = grownState;
        capacity = wanted;
        return true;
    }
    return growThroughHost(roundUp(required));
}

// Pinned memory for three words a ball, or null where the driver can't copy from it asynchronously. Waits only for
// the previous upload to have been read, which the GPU does long before the next frame's launches.
uint32_t* CudaBackend::uploadStaging(size_t balls) {
    const auto& api = driver.api();
    if (!api.memcpyHostToDeviceAsync || !stagingRead || balls == 0) {
        return nullptr;
    }
    api.eventSynchronize(stagingRead);
    if (stagingBalls < balls) {
        constexpr size_t minimumStagingBalls = size_t{1} << 16;
        const size_t wanted = std::max({balls, stagingBalls * 2, minimumStagingBalls});
        if (staging) {
            api.memFreeHost(staging);
        }
        staging = nullptr;
        stagingBalls = 0;
        void* memory = nullptr;
        if (api.memAllocHost(&memory, wanted * 3 * sizeof(uint32_t)) != CUDA_SUCCESS) {
            return nullptr;
        }
        staging = static_cast<uint32_t*>(memory);
        stagingBalls = wanted;
    }
    return staging;
}

// Each array may grow to all of device memory; addresses cost nothing until memory is mapped onto them.
bool CudaBackend::reserveGrowableArrays() {
    if (!GrowableDeviceArray::supported(driver)) {
        return false;
    }
    size_t freeBytes = 0;
    size_t totalBytes = 0;
    if (driver.api().memGetInfo(&freeBytes, &totalBytes) != CUDA_SUCCESS) {
        return false;
    }
    for (GrowableDeviceArray* array : {&positionArray, &stateArray, &sortedPositionArray, &sortedStateArray}) {
        if (!array->reserve(driver, totalBytes)) {
            for (GrowableDeviceArray* reserved : {&positionArray, &stateArray, &sortedPositionArray, &sortedStateArray}) {
                reserved->release();
            }
            return false;
        }
    }
    balls.position = positionArray.pointer();
    balls.state = stateArray.pointer();
    sortedPosition = sortedPositionArray.pointer();
    sortedState = sortedStateArray.pointer();
    return true;
}

// Mapped memory grows in fixed steps: nothing is copied, so there is no reason to grow geometrically, and mapping a
// quarter of a large array at once took one frame 165 ms.
// Balls come before sorting: when memory runs out, the sort buffers give theirs back and the growth is tried again.
bool CudaBackend::growInPlaceTo(size_t required) {
    const auto roundUp = [](size_t balls) { return (balls + capacityGranule - 1) / capacityGranule * capacityGranule; };
    const auto grow = [&](size_t wanted) {
        return positionArray.growTo(wanted * 4) && stateArray.growTo(wanted * 4);
    };
    for (const size_t wanted : {roundUp(std::max(required, capacity + growInPlaceBalls)), roundUp(required)}) {
        if (grow(wanted)) {
            capacity = wanted;
            return true;
        }
        releaseSortBuffers();
        if (grow(wanted)) {
            capacity = wanted;
            return true;
        }
    }
    return false;
}

// Near the top of device memory the old and new arrays do not fit side by side, so the balls wait in host memory
// while the device arrays are replaced. Only filling to capacity grows this far, so the copy's cost is a one-off.
bool CudaBackend::growThroughHost(size_t wanted) {
    if (fixedCapacity) {
        return false;
    }
    const auto& api = driver.api();
    std::vector<uint32_t> heldPosition(count);
    std::vector<uint32_t> heldState(count);
    if (count > 0 &&
        !(succeeded(api.memcpyDeviceToHost(heldPosition.data(), balls.position, count * 4), "stage position") &&
          succeeded(api.memcpyDeviceToHost(heldState.data(), balls.state, count * 4), "stage state"))) {
        return false;
    }
    api.memFree(balls.position);
    api.memFree(balls.state);
    balls.position = 0;
    balls.state = 0;
    const size_t previousCapacity = capacity;
    capacity = 0;
    bool grown = false;
    for (const size_t attempt : {wanted, previousCapacity}) {
        CUdeviceptr position = 0;
        CUdeviceptr state = 0;
        if (api.memAlloc(&position, attempt * 4) != CUDA_SUCCESS) {
            continue;
        }
        if (api.memAlloc(&state, attempt * 4) != CUDA_SUCCESS) {
            api.memFree(position);
            continue;
        }
        balls.position = position;
        balls.state = state;
        capacity = attempt;
        grown = attempt == wanted;
        break;
    }
    if (capacity < count) {
        fault = "device memory lost while growing the ball arrays";
        return false;
    }
    if (count > 0) {
        succeeded(api.memcpyHostToDevice(balls.position, heldPosition.data(), count * 4), "restore position") &&
            succeeded(api.memcpyHostToDevice(balls.state, heldState.data(), count * 4), "restore state");
    }
    return grown;
}

void CudaBackend::ensurePackedCapacity(size_t required) {
    if (isFaulted() || required <= packedCapacity) {
        return;
    }
    const auto& api = driver.api();
    const size_t newCapacity = (required + capacityGranule - 1) / capacityGranule * capacityGranule;
    if (balls.packed) {
        api.memFree(balls.packed);
        balls.packed = 0;
    }
    packedCapacity = 0;
    if (succeeded(api.memAlloc(&balls.packed, newCapacity * 4), "cuMemAlloc packed")) {
        packedCapacity = newCapacity;
    }
}

void CudaBackend::uploadBoard(const BoardUpload& board) {
    if (isFaulted()) {
        return;
    }
    driver.makeCurrent();
    const bool wholeBoard = !board.onlyChangedCells || uploadedCellWords != board.paddedCells.size();
    // The board's arrays come out of the reserve. A board too big for its share of it (or the first board after
    // one) would leave the renderer short, so while no balls are held, which is when a level loads, the ball
    // arrays are carved again around the new board's: smaller for a huge board, back to full for a normal one.
    // The capacity then depends on the board alone, and nothing is copied.
    const size_t boardArrayBytes = fixedCapacity && wholeBoard ? boardBytes(board) : 0;
    const bool recarve = fixedCapacity && count == 0 && wholeBoard &&
                         (capacity != preallocatedCapacity || boardArrayBytes > boardBytesInReserve);
    if (recarve) {
        freeBallArrays();
        releaseArray(cellWords);
        if (!board.targetCells.empty()) {
            releaseTargets();
        }
        if (!board.brickHitsLeft.empty()) {
            for (DeviceArray* array : {&brickHitsLeft, &killedIndices, &holes}) {
                releaseArray(*array);
            }
        }
    }
    if (wholeBoard) {
        uploadWholeBoard(board);
    } else {
        applyChangedCellWords(board);
    }
    if (!board.targetCells.empty()) {
        uploadTargets(board.targetCells);
    }
    scatterUpdates(board.targetUpdates, targetPointer(), targetUpdates);
    if (!board.brickHitsLeft.empty()) {
        uploadBrickHits(board.brickHitsLeft);
    }
    if (scatterUpdates(board.brickHitUpdates, brickHitsLeft.pointer, brickHitUpdates)) {
        for (size_t i = 0; i + 1 < board.brickHitUpdates.size(); i += 2) {
            hostBrickHitsLeft[static_cast<size_t>(board.brickHitUpdates[i])] = board.brickHitUpdates[i + 1];
        }
    }
    if (recarve) {
        carveBallArraysAfterBoard(boardArrayBytes);
    }
    fieldWidth = static_cast<float>(board.columns);
    fieldHeight = static_cast<float>(board.rows);
    chooseSortGrid();
}

// Writes (index, value) pairs into a device array with the cell-word kernel; false when there was nothing to write.
bool CudaBackend::scatterUpdates(std::span<const int32_t> pairs, CUdeviceptr target, DeviceArray& staging) {
    if (pairs.empty() || target == 0) {
        return false;
    }
    upload(staging, pairs.data(), pairs.size_bytes());
    CUdeviceptr updatesPointer = staging.pointer;
    int updateCount = static_cast<int>(pairs.size() / 2);
    void* arguments[] = {&updatesPointer, &updateCount, &target};
    return launch(applyCellWordsFunction, static_cast<size_t>(updateCount), arguments, "scatter board updates");
}

void CudaBackend::applyChangedCellWords(const BoardUpload& board) {
    if (board.changedCells.empty()) {
        return;
    }
    const std::vector<int32_t> updates = buildCellWordUpdates(board);
    upload(cellWordUpdates, updates.data(), updates.size() * sizeof(int32_t));
    CUdeviceptr updatesPointer = cellWordUpdates.pointer;
    CUdeviceptr wordsPointer = cellWords.pointer;
    int updateCount = static_cast<int>(board.changedCells.size());
    void* arguments[] = {&updatesPointer, &updateCount, &wordsPointer};
    launch(applyCellWordsFunction, board.changedCells.size(), arguments, "applyCellWordsKernel");
}

void CudaBackend::uploadWholeBoard(const BoardUpload& board) {
    const std::vector<int32_t> words = buildCellWords(board);
    upload(cellWords, words.data(), words.size() * sizeof(int32_t));
    uploadedCellWords = words.size();
}

void CudaBackend::uploadBrickHits(std::span<const int32_t> hitsLeft) {
    if (isFaulted()) {
        return;
    }
    driver.makeCurrent();
    hostBrickHitsLeft.assign(hitsLeft.begin(), hitsLeft.end());
    brickCount = hostBrickHitsLeft.size();
    hostBrickHitsLeft.push_back(0);
    upload(brickHitsLeft, hostBrickHitsLeft.data(), hostBrickHitsLeft.size() * sizeof(int32_t));
    hostBrickHitsLeft.pop_back();
    BallCount totalHits = 0;
    for (int32_t hits : hostBrickHitsLeft) {
        totalHits += static_cast<BallCount>(std::max(hits, 0));
    }
    // A kill past the capacity puts its hit back for the next tick, so a level with billions of hits needs only
    // enough records for one tick's worth.
    killedCapacity = static_cast<unsigned int>(std::clamp<BallCount>(totalHits, minimumKilledCapacity, maximumKilledCapacity));
    ensureArray(killedIndices, static_cast<size_t>(killedCapacity) * killedRecordWords * 4);
    ensureArray(holes, static_cast<size_t>(killedCapacity) * 4);
}

// The Auto ramp hands over millions of balls a frame; one thread encoding them took most of its frame budget.
template <typename Encode>
void encodeInParallel(size_t count, const Encode& encode) {
    constexpr size_t chunkBalls = size_t{1} << 12;
    if (count <= chunkBalls * 4) {
        encode(0, count);
        return;
    }
    static WorkerPool pool(std::max(1u, std::thread::hardware_concurrency()));
    pool.runChunks(count, chunkBalls, [&](size_t, size_t begin, size_t end) { encode(begin, end); });
}

void CudaBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty() || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    if (!ensureBallCapacity(count + spawns.size()) && isFaulted()) {
        return;
    }
    const size_t added = std::min(spawns.size(), capacity - count);
    if (added == 0) {
        return;
    }
    uint32_t* pinned = uploadStaging(added);
    std::vector<uint32_t> pageable(pinned ? 0 : added * 3);
    uint32_t* const position = pinned ? pinned : pageable.data();
    uint32_t* const state = position + added;
    uint32_t* const packed = state + added;
    ballSpeed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeedScale = 1.0f / ballSpeed;
    const unsigned int ticksSinceEpoch = clockTick - freeFlightEpoch;
    // Encoding a spawn costs tens of nanoseconds once balls fly freely (each is projected back to the epoch), so
    // launches are spread over a pool that stays up between them.
    const auto encode = [&](size_t, size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const BallSpawn& spawn = spawns[i];
            const uint32_t color = static_cast<uint32_t>(spawn.color);
            int directionX = signedComponent(encodeComponent(spawn.vx * inverseSpeedScale));
            int directionY = signedComponent(encodeComponent(spawn.vy * inverseSpeedScale));
            float x = spawn.x;
            float y = spawn.y;
            if (frozen) {
                // Projected with the quantized direction the kernels decode, flipping only the signs, so flying
                // forward from the epoch lands on the spawn point.
                const float scale = ballSpeed / std::max(std::hypot(static_cast<float>(directionX),
                                                                    static_cast<float>(directionY)), 1.0f);
                const FreeFlightPosition epoch =
                    flyFreelyBackwards(x, y, static_cast<float>(directionX) * scale,
                                       static_cast<float>(directionY) * scale, ticksSinceEpoch, lastParams);
                x = epoch.x;
                y = epoch.y;
                directionX = (epoch.vx < 0.0f) == (directionX < 0) ? directionX : -directionX;
                directionY = (epoch.vy < 0.0f) == (directionY < 0) ? directionY : -directionY;
            }
            position[i] = encodeAxis(x, fieldWidth) | (encodeAxis(y, fieldHeight) << 16);
            state[i] = (static_cast<uint32_t>(directionX) & directionMask) |
                       ((static_cast<uint32_t>(directionY) & directionMask) << directionBits) | (color << colorShift);
            packed[i] = packBallForRender(spawn.x, spawn.y, spawn.color, renderPackScale(fieldWidth),
                                          renderPackScale(fieldHeight));
        }
    };
    if (added <= encodeChunkBalls) {
        encode(0, 0, added);
    } else {
        if (!encodePool) {
            encodePool = std::make_unique<WorkerPool>(std::max(1u, std::thread::hardware_concurrency()));
        }
        encodePool->runChunks(added, encodeChunkBalls, encode);
    }
    const auto& api = driver.api();
    const size_t offset = count * 4;
    const bool withPacked = count + added <= packedCapacity;
    if (pinned) {
        succeeded(api.memcpyHostToDeviceAsync(balls.position + offset, position, added * 4, nullptr), "upload position") &&
            succeeded(api.memcpyHostToDeviceAsync(balls.state + offset, state, added * 4, nullptr), "upload state") &&
            (!withPacked ||
             succeeded(api.memcpyHostToDeviceAsync(balls.packed + offset, packed, added * 4, nullptr), "upload packed"));
        api.eventRecord(stagingRead, nullptr);
    } else {
        succeeded(api.memcpyHostToDevice(balls.position + offset, position, added * 4), "upload position");
        succeeded(api.memcpyHostToDevice(balls.state + offset, state, added * 4), "upload state");
        if (withPacked) {
            succeeded(api.memcpyHostToDevice(balls.packed + offset, packed, added * 4), "upload packed");
        }
    }
    count += added;
    sortSchedule.ballsAdded(added, count);
    snapshotFresh = false;
}

bool CudaBackend::prepareVisibilityGrid(DeviceVisibilityGridArgument& grid) {
    const size_t cells = gridCellCount();
    const bool renderBufferFitsGrid = !renderResource || renderCapacityBytes >= cells * 4;
    if (cells == 0 || count <= cells || !renderBufferFitsGrid) {
        return false;
    }
    ensureArray(gridSlots, cells * sizeof(unsigned long long));
    if (isFaulted() || !succeeded(driver.api().memsetD32(gridSlots.pointer, 0, cells * 2), "clear grid")) {
        return false;
    }
    const float coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateMax + 1);
    const float coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateMax + 1);
    grid = {gridSlots.pointer, gridWidth, gridHeight, coordinateToSlotX, coordinateToSlotY, ++frameSeed,
            bidderMaskFor(count, cells)};
    gridFilled = true;
    return true;
}

void CudaBackend::renderFreeFlight(const StepParams& params) {
    if (!frozen) {
        frozen = true;
        freeFlightEpoch = params.tick;
        releaseSortBuffers();
    }
    if (!drawing) {
        lastStepKernelMs = 0.0;
        return;
    }
    const auto& api = driver.api();
    if (stepStarted) {
        api.eventRecord(stepStarted, nullptr);
    }
    DeviceVisibilityGridArgument grid{0, gridWidth, gridHeight, 0.0f, 0.0f, 0, 0};
    unsigned int chunkStride = 1;
    if (prepareVisibilityGrid(grid)) {
        chunkStride = grid.bidderMask + 1;
        grid.bidderMask = 0;
    } else {
        ensurePackedCapacity(count);
    }
    const size_t chunks = (count + threadsPerBlock - 1) / threadsPerBlock;
    unsigned int chunkOffset = frameSeed % chunkStride;
    const size_t sampledChunks = chunks > chunkOffset ? (chunks - chunkOffset + chunkStride - 1) / chunkStride : 0;
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    unsigned int ticksArgument = clockTick - freeFlightEpoch;
    unsigned int packedCapacityArgument = static_cast<unsigned int>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu));
    void* arguments[] = {&balls, &ballCountArgument, &stepArgument, &ticksArgument, &grid,
                         &packedCapacityArgument, &chunkStride, &chunkOffset};
    if (sampledChunks > 0 &&
        !succeeded(api.launchKernel(freeFlightRenderFunction, static_cast<unsigned int>(sampledChunks), 1, 1,
                                    threadsPerBlock, 1, 1, 0, nullptr, arguments, nullptr),
                   "freeFlightRenderKernel")) {
        return;
    }
    if (stepStarted && stepFinished) {
        api.eventRecord(stepFinished, nullptr);
        api.eventSynchronize(stepFinished);
        float elapsedMs = 0.0f;
        api.eventElapsedTime(&elapsedMs, stepStarted, stepFinished);
        lastStepKernelMs = elapsedMs;
    }
}

void CudaBackend::materializeFreeFlight() {
    driver.makeCurrent();
    if (!frozen) {
        return;
    }
    frozen = false;
    if (count == 0 || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = lastParams;
    unsigned int ticksArgument = clockTick - freeFlightEpoch;
    void* arguments[] = {&balls, &ballCountArgument, &stepArgument, &ticksArgument};
    launch(materializeFreeFlightFunction, count, arguments, "materializeFreeFlightKernel");
}

size_t CudaBackend::gridCellCount() const { return static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight); }

void CudaBackend::setVisibilityGrid(int width, int height) {
    gridWidth = std::max(width, 0);
    gridHeight = std::max(height, 0);
}

void CudaBackend::step(const StepParams& params, std::vector<BallHit>&) {
    snapshotFresh = false;
    gridFilled = false;
    lastCounters = {};
    if (count == 0 || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    lastParams = params;
    clockTick = params.tick + static_cast<unsigned int>(params.substeps);
    if (params.freeFlight) {
        renderFreeFlight(params);
        return;
    }
    materializeFreeFlight();
    const auto& api = driver.api();
    if (!succeeded(api.memsetD32(counters.pointer, 0, sizeof(FrameCounters) / 4), "reset counters")) {
        return;
    }
    CUdeviceptr cellWordsPointer = cellWords.pointer;
    CUdeviceptr targetCellsPointer = targetPointer();
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    CUdeviceptr hitsLeftPointer = brickHitsLeft.pointer;
    CUdeviceptr killedPointer = killedIndices.pointer;
    CUdeviceptr countersPointer = counters.pointer;
    DeviceVisibilityGridArgument grid{0, 0, 0, 0.0f, 0.0f, 0, 0};
    if (drawing) {
        grid.width = gridWidth;
        grid.height = gridHeight;
        if (!prepareVisibilityGrid(grid)) {
            ensurePackedCapacity(count);
        }
    }
    unsigned int killedCapacityArgument = killedCapacity;
    unsigned int packedCapacityArgument =
        drawing ? static_cast<unsigned int>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu)) : 0u;
    DeviceSortedOutput sorted{};
    void* arguments[] = {&balls,           &ballCountArgument, &cellWordsPointer, &targetCellsPointer,
                         &stepArgument,    &hitsLeftPointer,   &killedPointer,    &killedCapacityArgument,
                         &countersPointer, &grid,              &packedCapacityArgument, &sorted};
    if (stepStarted) {
        api.eventRecord(stepStarted, nullptr);
    }
    if (sortSchedule.sortThisTick(count)) {
        if (countBallsByCell()) {
            sorted = {cellCursors.pointer, sortedPosition, sortedState, sortColumns, sortRows};
        }
    }
    const bool sorting = sorted.columns > 0;
    const bool launched =
        sorting ? succeeded(api.launchKernel(stepAndSortFunction,
                                             static_cast<unsigned int>((count + sortBallsPerBlock - 1) / sortBallsPerBlock),
                                             1, 1, stepSortThreads, 1, 1, 0, nullptr, arguments, nullptr),
                            "stepAndSortBallsKernel")
                : launch(stepFunction, (count + ballsPerStepThread - 1) / ballsPerStepThread, arguments, "stepBallsKernel");
    if (!launched) {
        return;
    }
    if (sorting) {
        std::swap(balls.position, sortedPosition);
        std::swap(balls.state, sortedState);
        if (growInPlace) {
            std::swap(positionArray, sortedPositionArray);
            std::swap(stateArray, sortedStateArray);
        }
    }
    if (stepFinished) {
        api.eventRecord(stepFinished, nullptr);
    }
    if (!succeeded(api.memcpyDeviceToHost(&lastCounters, counters.pointer, sizeof(FrameCounters)), "download counters")) {
        return;
    }
    if (stepStarted && stepFinished) {
        float elapsedMs = 0.0f;
        api.eventElapsedTime(&elapsedMs, stepStarted, stepFinished);
        lastStepKernelMs = elapsedMs;
        sortSchedule.record(elapsedMs, count);
    }
    lastCounters.killedCount = std::min(lastCounters.killedCount, killedCapacity);
    if (lastCounters.killedCount == 0) {
        return;
    }
    compactKilledBalls(lastCounters.killedCount);
    hostHitBricks.resize(lastCounters.killedCount);
    succeeded(api.memcpyDeviceToHost(hostHitBricks.data(), killedIndices.pointer + static_cast<size_t>(killedCapacity) * 4,
                                     hostHitBricks.size() * sizeof(uint32_t)),
              "download hit bricks");
}

void CudaBackend::compactKilledBalls(unsigned int killed) {
    const unsigned int oldCount = static_cast<unsigned int>(count);
    unsigned int survivorCount = oldCount - killed;
    CUdeviceptr killedPointer = killedIndices.pointer;
    CUdeviceptr holesPointer = holes.pointer;
    CUdeviceptr countersPointer = counters.pointer;
    void* collectArguments[] = {&killedPointer, &killed, &survivorCount, &holesPointer, &countersPointer};
    if (!launch(collectHolesFunction, killed, collectArguments, "collectHolesKernel")) {
        return;
    }
    unsigned int oldCountArgument = oldCount;
    unsigned int packedCapacityArgument = static_cast<unsigned int>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu));
    void* fillArguments[] = {&balls, &survivorCount, &oldCountArgument, &holesPointer, &countersPointer,
                             &packedCapacityArgument};
    if (!launch(fillHolesFunction, killed, fillArguments, "fillHolesKernel")) {
        return;
    }
    count = survivorCount;
}

DeviceHitResults CudaBackend::deviceHitResults() const {
    DeviceHitResults results;
    if (lastCounters.killedCount == 0) {
        return results;
    }
    results.hitBricks = hostHitBricks;
    for (int color = 0; color < ballColorCount; ++color) {
        results.killsByColor[color] = lastCounters.killsByColor[color];
    }
    results.killed = lastCounters.killedCount;
    return results;
}

void CudaBackend::removeBalls(std::span<const uint32_t>) {}

BallTally CudaBackend::truncate(size_t newCount) {
    if (isFaulted() || newCount >= count) {
        return {};
    }
    driver.makeCurrent();
    const BallTally removed = downloadColorTally(newCount, count);
    count = newCount;
    snapshotFresh = false;
    return removed;
}

void CudaBackend::releaseRenderBuffer() {
    if (renderResource) {
        driver.api().graphicsUnregisterResource(renderResource);
        renderResource = nullptr;
    }
    renderCapacityBytes = 0;
}

bool CudaBackend::shareRenderBuffer(unsigned int openGlBuffer, size_t capacityBytes) {
    if (isFaulted() || !driver.api().graphicsGlRegisterBuffer) {
        return false;
    }
    driver.makeCurrent();
    releaseRenderBuffer();
    if (driver.api().graphicsGlRegisterBuffer(&renderResource, openGlBuffer, writeDiscardFlag) != CUDA_SUCCESS) {
        renderResource = nullptr;
        return false;
    }
    renderCapacityBytes = capacityBytes;
    return true;
}

bool CudaBackend::copyIntoRenderBuffer(CUdeviceptr source, size_t elements) {
    if (!renderResource || elements * 4 > renderCapacityBytes) {
        return false;
    }
    const auto& api = driver.api();
    if (api.graphicsMapResources(1, &renderResource, nullptr) != CUDA_SUCCESS) {
        return false;
    }
    CUdeviceptr mapped = 0;
    size_t mappedBytes = 0;
    bool copied = api.graphicsResourceGetMappedPointer(&mapped, &mappedBytes, renderResource) == CUDA_SUCCESS &&
                  mappedBytes >= elements * 4 &&
                  succeeded(api.memcpyDeviceToDevice(mapped, source, elements * 4), "copy to render buffer");
    api.graphicsUnmapResources(1, &renderResource, nullptr);
    return copied;
}

bool CudaBackend::writeGridIntoRenderBuffer() {
    const size_t cells = gridCellCount();
    if (!renderResource || cells * 4 > renderCapacityBytes) {
        return false;
    }
    const auto& api = driver.api();
    if (api.graphicsMapResources(1, &renderResource, nullptr) != CUDA_SUCCESS) {
        return false;
    }
    CUdeviceptr mapped = 0;
    size_t mappedBytes = 0;
    bool written = false;
    if (api.graphicsResourceGetMappedPointer(&mapped, &mappedBytes, renderResource) == CUDA_SUCCESS &&
        mappedBytes >= cells * 4) {
        CUdeviceptr slotsPointer = gridSlots.pointer;
        int cellsArgument = static_cast<int>(cells);
        void* arguments[] = {&slotsPointer, &cellsArgument, &mapped};
        written = launch(gridToRenderFunction, cells, arguments, "gridToRenderKernel");
    }
    api.graphicsUnmapResources(1, &renderResource, nullptr);
    return written;
}

BallTally CudaBackend::downloadColorTally(size_t first, size_t last) {
    BallTally tally{};
    if (first >= last) {
        return tally;
    }
    std::vector<uint32_t> states(last - first);
    if (succeeded(driver.api().memcpyDeviceToHost(states.data(), balls.state + first * 4, states.size() * 4),
                  "download state")) {
        for (uint32_t value : states) {
            const uint32_t color = stateColor(value);
            if (color != deadColor) {
                ++tally[color];
            }
        }
    }
    return tally;
}

void CudaBackend::downloadPacked() {
    hostPacked.resize(std::min(count, packedCapacity));
    if (!hostPacked.empty()) {
        succeeded(driver.api().memcpyDeviceToHost(hostPacked.data(), balls.packed, hostPacked.size() * 4),
                  "download packed");
    }
}

bool CudaBackend::downloadGrid() {
    const size_t cells = gridCellCount();
    ensureArray(gridPacked, cells * 4);
    if (isFaulted()) {
        return false;
    }
    CUdeviceptr slotsPointer = gridSlots.pointer;
    CUdeviceptr packedPointer = gridPacked.pointer;
    int cellsArgument = static_cast<int>(cells);
    void* arguments[] = {&slotsPointer, &cellsArgument, &packedPointer};
    if (!launch(gridToRenderFunction, cells, arguments, "gridToRenderKernel")) {
        return false;
    }
    hostPacked.resize(cells);
    return succeeded(driver.api().memcpyDeviceToHost(hostPacked.data(), gridPacked.pointer, cells * 4), "download grid");
}

void CudaBackend::releaseSortBuffers() {
    if (fixedCapacity) {
        return;
    }
    if (growInPlace) {
        sortedPositionArray.shrinkTo(0);
        sortedStateArray.shrinkTo(0);
        sortedCapacity = 0;
        return;
    }
    for (CUdeviceptr* buffer : {&sortedPosition, &sortedState}) {
        if (*buffer) {
            driver.api().memFree(*buffer);
            *buffer = 0;
        }
    }
    sortedCapacity = 0;
}

bool CudaBackend::ensureSortBuffers() {
    if (growInPlace) {
        if (sortedCapacity == capacity) {
            return true;
        }
        if (sortedPositionArray.growTo(capacity * 4) && sortedStateArray.growTo(capacity * 4)) {
            sortedCapacity = capacity;
            return true;
        }
        releaseSortBuffers();
        return false;
    }
    if (sortedCapacity == capacity && sortedPosition && sortedState) {
        return true;
    }
    if (fixedCapacity) {
        return sortedPosition && sortedState && sortedCapacity >= capacity;
    }
    const auto& api = driver.api();
    releaseSortBuffers();
    if (api.memAlloc(&sortedPosition, capacity * 4) != CUDA_SUCCESS ||
        api.memAlloc(&sortedState, capacity * 4) != CUDA_SUCCESS) {
        releaseSortBuffers();
        return false;
    }
    sortedCapacity = capacity;
    return true;
}

void CudaBackend::chooseSortGrid() {
    const int columns = std::max(static_cast<int>(fieldWidth), 1);
    const int rows = std::max(static_cast<int>(fieldHeight), 1);
    int cellsPerBucketSide = 1;
    const auto bucketsAlong = [&](int cells) { return (cells + cellsPerBucketSide - 1) / cellsPerBucketSide; };
    while (bucketsAlong(columns) * bucketsAlong(rows) > maxSortCells) {
        ++cellsPerBucketSide;
    }
    sortColumns = bucketsAlong(columns);
    sortRows = bucketsAlong(rows);
}

bool CudaBackend::countBallsByCell() {
    const int columns = sortColumns;
    const int rows = sortRows;
    const int cellCount = columns * rows;
    if (count == 0 || cellCount <= 0 || cellCount > maxSortCells || !ensureSortBuffers()) {
        return false;
    }
    ensureArray(cellCursors, static_cast<size_t>(cellCount) * 4);
    const auto& api = driver.api();
    if (isFaulted() || !succeeded(api.memsetD32(cellCursors.pointer, 0, static_cast<size_t>(cellCount)), "clear cells")) {
        return false;
    }
    CUdeviceptr positionPointer = balls.position;
    CUdeviceptr cursorPointer = cellCursors.pointer;
    int countArgument = static_cast<int>(count);
    int columnsArgument = columns;
    int rowsArgument = rows;
    int cellCountArgument = cellCount;
    const size_t countThreads = (count + sortBallsPerBlock - 1) / sortBallsPerBlock * threadsPerBlock;
    void* countArguments[] = {&positionPointer, &countArgument, &columnsArgument, &rowsArgument, &cursorPointer};
    void* scanArguments[] = {&cursorPointer, &cellCountArgument};
    return launch(countCellsFunction, countThreads, countArguments, "countCellsKernel") &&
           launch(scanCellsFunction, threadsPerBlock, scanArguments, "scanCellsKernel");
}

BallSnapshot CudaBackend::snapshot() {
    if (isFaulted()) {
        return {};
    }
    driver.makeCurrent();
    if (gridFilled && renderResource && writeGridIntoRenderBuffer()) {
        return {nullptr, gridCellCount(), fieldWidth, fieldHeight, true, true};
    }
    if (gridFilled && !renderResource) {
        if (!snapshotFresh && downloadGrid()) {
            snapshotFresh = true;
        }
        return snapshotFresh ? BallSnapshot{hostPacked.data(), hostPacked.size(), fieldWidth, fieldHeight, false, true}
                             : BallSnapshot{};
    }
    if (count > 0 && count <= packedCapacity && copyIntoRenderBuffer(balls.packed, count)) {
        return {nullptr, count, fieldWidth, fieldHeight, true, false};
    }
    if (!snapshotFresh) {
        downloadPacked();
        snapshotFresh = true;
    }
    if (isFaulted()) {
        return {};
    }
    return {hostPacked.data(), hostPacked.size(), fieldWidth, fieldHeight, false};
}

std::vector<BallSpawn> CudaBackend::exportBalls() { return exportRange(0); }

std::vector<BallSpawn> CudaBackend::takeTail(size_t tailCount) {
    const size_t first = count - std::min(tailCount, count);
    std::vector<BallSpawn> tail = exportRange(first);
    if (!isFaulted()) {
        count = first;
        snapshotFresh = false;
    }
    return tail;
}

bool CudaBackend::takeCompactTail(size_t tailCount, CompactBalls& taken) {
    driver.makeCurrent();
    materializeFreeFlight();
    if (isFaulted()) {
        return false;
    }
    driver.makeCurrent();
    const size_t first = count - std::min(tailCount, count);
    const size_t takenCount = count - first;
    taken.position.resize(takenCount);
    taken.state.resize(takenCount);
    const auto& api = driver.api();
    const bool downloaded =
        takenCount == 0 ||
        (succeeded(api.memcpyDeviceToHost(taken.position.data(), balls.position + first * 4, takenCount * 4),
                   "download position") &&
         succeeded(api.memcpyDeviceToHost(taken.state.data(), balls.state + first * 4, takenCount * 4), "download state"));
    if (!downloaded) {
        return false;
    }
    count = first;
    snapshotFresh = false;
    return true;
}

bool CudaBackend::addCompact(const CompactBalls& added) {
    driver.makeCurrent();
    materializeFreeFlight();
    const size_t availableRoom = capacity > count ? capacity - count : 0;
    const size_t addedCount = std::min({added.position.size(), added.state.size(),
                                        fixedCapacity ? availableRoom : std::numeric_limits<size_t>::max()});
    if (isFaulted()) {
        return false;
    }
    if (addedCount == 0) {
        return true;
    }
    driver.makeCurrent();
    if (!ensureBallCapacity(count + addedCount)) {
        return false;
    }
    const auto& api = driver.api();
    const size_t offset = count * 4;
    succeeded(api.memcpyHostToDevice(balls.position + offset, added.position.data(), addedCount * 4), "upload position");
    succeeded(api.memcpyHostToDevice(balls.state + offset, added.state.data(), addedCount * 4), "upload state");
    if (count + addedCount <= packedCapacity) {
        std::vector<uint32_t> packed(addedCount);
        for (size_t i = 0; i < addedCount; ++i) {
            packed[i] = packCompactForRender(added.position[i], added.state[i]);
        }
        succeeded(api.memcpyHostToDevice(balls.packed + offset, packed.data(), addedCount * 4), "upload packed");
    }
    count += addedCount;
    sortSchedule.ballsAdded(addedCount, count);
    snapshotFresh = false;
    return !isFaulted();
}

std::vector<BallSpawn> CudaBackend::exportRange(size_t first) {
    driver.makeCurrent();
    materializeFreeFlight();
    if (isFaulted() || first >= count) {
        return {};
    }
    driver.makeCurrent();
    const size_t rangeCount = count - first;
    std::vector<uint32_t> positions(rangeCount), states(rangeCount);
    const auto& api = driver.api();
    const bool downloaded =
        succeeded(api.memcpyDeviceToHost(positions.data(), balls.position + first * 4, rangeCount * 4), "download position") &&
        succeeded(api.memcpyDeviceToHost(states.data(), balls.state + first * 4, rangeCount * 4), "download state");
    if (!downloaded) {
        return {};
    }
    std::vector<BallSpawn> exported;
    exported.reserve(rangeCount);
    for (size_t i = 0; i < rangeCount; ++i) {
        const uint32_t color = stateColor(states[i]);
        if (color == deadColor) {
            continue;
        }
        const float dx = static_cast<float>(signedComponent(states[i] & directionMask));
        const float dy = static_cast<float>(signedComponent((states[i] >> directionBits) & directionMask));
        const float length = std::max(std::hypot(dx, dy), 1.0f);
        exported.push_back({static_cast<float>(positions[i] & positionMask) * fieldWidth / positionScale,
                            static_cast<float>(positions[i] >> 16) * fieldHeight / positionScale,
                            dx / length * ballSpeed, dy / length * ballSpeed, static_cast<int32_t>(color)});
    }
    return exported;
}
