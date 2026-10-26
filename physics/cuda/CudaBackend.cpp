#include "cuda/CudaBackend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr unsigned int threadsPerBlock = 256;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr unsigned int writeDiscardFlag = 0x02;
constexpr size_t stateArrayCount = 2;
constexpr size_t capacityGranule = size_t{1} << 20;
constexpr size_t growthDivisor = 4;
constexpr unsigned int minimumKilledCapacity = 1024;
constexpr float positionScale = 65535.0f;
constexpr unsigned int positionMask = 0xFFFFu;
constexpr int directionBits = 13;
constexpr unsigned int directionMask = (1u << directionBits) - 1u;
constexpr float directionScale = 4095.0f;
constexpr unsigned int colorShift = 26u;
constexpr unsigned int colorMask = 7u;
constexpr unsigned int deadColor = 7u;
constexpr int maxSortCells = 4096;
constexpr size_t sortBallsPerBlock = 4096;
constexpr unsigned int stepSortThreads = 512;
constexpr size_t ballsPerStepThread = 2;
constexpr std::array<int, 3> sortIntervalCandidates{0, 8, 16};
constexpr int sortTrialWarmupTicks = 16;
constexpr int sortTrialMeasuredTicks = 32;
constexpr int ticksBetweenSortTrials = 1200;
constexpr size_t minimumBallsForSorting = size_t{1} << 20;

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

struct DeviceVisibilityGrid {
    CUdeviceptr slots;
    int width;
    int height;
    float scaleX;
    float scaleY;
    unsigned int frameSeed;
    unsigned int bidderMask;
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

std::vector<int32_t> buildCellWords(const BoardUpload& board) {
    std::vector<int32_t> words(board.paddedCells.size(), emptyFarCell);
    for (size_t cell = 0; cell < words.size(); ++cell) {
        const int32_t brick = board.paddedCells[cell];
        if (brick >= 0) {
            words[cell] = (brick << cellBrickShift) | board.brickColors[static_cast<size_t>(brick)];
        } else if (board.nearBrickCells[cell] != 0) {
            words[cell] = emptyNearCell;
        }
    }
    return words;
}

unsigned int blocksFor(size_t threads) {
    return static_cast<unsigned int>((threads + threadsPerBlock - 1) / threadsPerBlock);
}

}

std::string CudaBackend::unavailableReason() {
    const CudaDriver& driver = CudaDriver::instance();
    return driver.isReady() ? std::string{} : driver.error();
}

CudaBackend::CudaBackend() : driver(CudaDriver::instance()) {
    driver.makeCurrent();
    stepFunction = driver.function("stepBallsKernel");
    collectHolesFunction = driver.function("collectHolesKernel");
    fillHolesFunction = driver.function("fillHolesKernel");
    gridToRenderFunction = driver.function("gridToRenderKernel");
    countCellsFunction = driver.function("countCellsKernel");
    scanCellsFunction = driver.function("scanCellsKernel");
    stepAndSortFunction = driver.function("stepAndSortBallsKernel");
    if (!stepFunction || !collectHolesFunction || !fillHolesFunction || !gridToRenderFunction ||
        !countCellsFunction || !scanCellsFunction || !stepAndSortFunction) {
        fault = "CUDA kernels missing from module";
        return;
    }
    ensureBallCapacity(initialBallCapacity);
    ensureArray(counters, sizeof(FrameCounters));
    if (driver.api().eventCreate) {
        driver.api().eventCreate(&stepStarted, CU_EVENT_DEFAULT);
        driver.api().eventCreate(&stepFinished, CU_EVENT_DEFAULT);
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
    for (CUevent event : {stepStarted, stepFinished}) {
        if (event && api.eventDestroy) {
            api.eventDestroy(event);
        }
    }
    for (CUdeviceptr pointer :
         {balls.position, balls.state, balls.packed, paddedCells.pointer,
          brickColors.pointer, targetCells.pointer, cellWords.pointer, brickHitsLeft.pointer, killedIndices.pointer, holes.pointer,
          counters.pointer, gridSlots.pointer, gridPacked.pointer, cellCursors.pointer, sortedPosition, sortedState}) {
        if (pointer) {
            api.memFree(pointer);
        }
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

void CudaBackend::ensureBallCapacity(size_t required) {
    if (isFaulted() || required <= capacity) {
        return;
    }
    releaseSortBuffers();
    const auto& api = driver.api();
    const size_t grown = std::max(required, capacity + capacity / growthDivisor);
    const size_t newCapacity = (grown + capacityGranule - 1) / capacityGranule * capacityGranule;
    const std::array<CUdeviceptr*, stateArrayCount> fields{&balls.position, &balls.state};
    for (CUdeviceptr* field : fields) {
        CUdeviceptr grownArray = 0;
        if (!succeeded(api.memAlloc(&grownArray, newCapacity * 4), "cuMemAlloc")) {
            return;
        }
        if (*field) {
            if (count > 0) {
                succeeded(api.memcpyDeviceToDevice(grownArray, *field, count * 4), "cuMemcpyDtoD");
            }
            api.memFree(*field);
        }
        *field = grownArray;
    }
    capacity = newCapacity;
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
    upload(paddedCells, board.paddedCells.data(), board.paddedCells.size_bytes());
    std::vector<int32_t> colors(board.brickColors.begin(), board.brickColors.end());
    colors.push_back(-1);
    upload(brickColors, colors.data(), colors.size() * sizeof(int32_t));
    upload(targetCells, board.targetCells.data(), board.targetCells.size_bytes());
    const std::vector<int32_t> words = buildCellWords(board);
    upload(cellWords, words.data(), words.size() * sizeof(int32_t));
    hostBrickHitsLeft.assign(board.brickHitsLeft.begin(), board.brickHitsLeft.end());
    brickCount = hostBrickHitsLeft.size();
    hostBrickHitsLeft.push_back(0);
    upload(brickHitsLeft, hostBrickHitsLeft.data(), hostBrickHitsLeft.size() * sizeof(int32_t));
    hostBrickHitsLeft.pop_back();
    BallCount totalHits = 0;
    for (int32_t hits : hostBrickHitsLeft) {
        totalHits += static_cast<BallCount>(std::max(hits, 0));
    }
    killedCapacity = static_cast<unsigned int>(std::max<BallCount>(totalHits, minimumKilledCapacity));
    ensureArray(killedIndices, static_cast<size_t>(killedCapacity) * 4);
    ensureArray(holes, static_cast<size_t>(killedCapacity) * 4);
    fieldWidth = static_cast<float>(board.columns);
    fieldHeight = static_cast<float>(board.rows);
}

void CudaBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty() || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    ensureBallCapacity(count + spawns.size());
    if (isFaulted()) {
        return;
    }
    const size_t added = spawns.size();
    std::vector<uint32_t> position(added), state(added), packed(added);
    ballSpeed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeedScale = 1.0f / ballSpeed;
    for (size_t i = 0; i < added; ++i) {
        const BallSpawn& spawn = spawns[i];
        const uint32_t color = static_cast<uint32_t>(spawn.color);
        position[i] = encodeAxis(spawn.x, fieldWidth) | (encodeAxis(spawn.y, fieldHeight) << 16);
        state[i] = encodeComponent(spawn.vx * inverseSpeedScale) |
                   (encodeComponent(spawn.vy * inverseSpeedScale) << directionBits) | (color << colorShift);
        packed[i] = packBallForRender(spawn.x, spawn.y, spawn.color, renderPackScale(fieldWidth),
                                      renderPackScale(fieldHeight));
    }
    const auto& api = driver.api();
    const size_t offset = count * 4;
    succeeded(api.memcpyHostToDevice(balls.position + offset, position.data(), added * 4), "upload position");
    succeeded(api.memcpyHostToDevice(balls.state + offset, state.data(), added * 4), "upload state");
    if (count + added <= packedCapacity) {
        succeeded(api.memcpyHostToDevice(balls.packed + offset, packed.data(), added * 4), "upload packed");
    }
    count += added;
    snapshotFresh = false;
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
    const auto& api = driver.api();
    if (!succeeded(api.memsetD32(counters.pointer, 0, sizeof(FrameCounters) / 4), "reset counters")) {
        return;
    }
    CUdeviceptr cellWordsPointer = cellWords.pointer;
    CUdeviceptr targetCellsPointer = targetCells.pointer;
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    CUdeviceptr hitsLeftPointer = brickHitsLeft.pointer;
    CUdeviceptr killedPointer = killedIndices.pointer;
    CUdeviceptr countersPointer = counters.pointer;
    DeviceVisibilityGrid grid{0, gridWidth, gridHeight, 0.0f, 0.0f, 0, 0};
    const size_t cells = gridCellCount();
    const bool renderBufferFitsGrid = !renderResource || renderCapacityBytes >= cells * 4;
    if (cells > 0 && count > cells && renderBufferFitsGrid) {
        ensureArray(gridSlots, cells * sizeof(unsigned long long));
        if (!isFaulted() && succeeded(api.memsetD32(gridSlots.pointer, 0, cells * 2), "clear grid")) {
            const float coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateMax + 1);
            const float coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateMax + 1);
            grid = {gridSlots.pointer, gridWidth,   gridHeight, coordinateToSlotX, coordinateToSlotY,
                    ++frameSeed,       bidderMaskFor(count, cells)};
            gridFilled = true;
        }
    }
    if (!gridFilled) {
        ensurePackedCapacity(count);
    }
    unsigned int killedCapacityArgument = killedCapacity;
    unsigned int packedCapacityArgument = static_cast<unsigned int>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu));
    DeviceSortedOutput sorted{};
    void* arguments[] = {&balls,           &ballCountArgument, &cellWordsPointer, &targetCellsPointer,
                         &stepArgument,    &hitsLeftPointer,   &killedPointer,    &killedCapacityArgument,
                         &countersPointer, &grid,              &packedCapacityArgument, &sorted};
    if (stepStarted) {
        api.eventRecord(stepStarted, nullptr);
    }
    if (count >= minimumBallsForSorting && sortIntervalTicks > 0 && ++ticksSinceSort >= sortIntervalTicks) {
        ticksSinceSort = 0;
        if (countBallsByCell()) {
            sorted = {cellCursors.pointer, sortedPosition, sortedState, static_cast<int>(fieldWidth),
                      static_cast<int>(fieldHeight)};
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
        updateSortSchedule(elapsedMs);
    }
    lastCounters.killedCount = std::min(lastCounters.killedCount, killedCapacity);
    if (lastCounters.killedCount == 0) {
        return;
    }
    compactKilledBalls(lastCounters.killedCount);
    if (brickCount > 0) {
        succeeded(api.memcpyDeviceToHost(hostBrickHitsLeft.data(), brickHitsLeft.pointer, brickCount * sizeof(int32_t)),
                  "download brick hits");
    }
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
    results.brickHitsLeft = std::span<const int32_t>(hostBrickHitsLeft.data(), brickCount);
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
    for (CUdeviceptr* buffer : {&sortedPosition, &sortedState}) {
        if (*buffer) {
            driver.api().memFree(*buffer);
            *buffer = 0;
        }
    }
    sortedCapacity = 0;
}

bool CudaBackend::ensureSortBuffers() {
    if (sortedCapacity == capacity && sortedPosition && sortedState) {
        return true;
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

void CudaBackend::updateSortSchedule(double kernelMs) {
    if (count < minimumBallsForSorting) {
        return;
    }
    if (ticksUntilSortTrial > 0) {
        --ticksUntilSortTrial;
        if (ticksUntilSortTrial == 0) {
            sortCandidate = 0;
            sortTrialTick = 0;
            sortTrialMsPerBall = 0.0;
            sortIntervalTicks = sortIntervalCandidates[0];
            ticksSinceSort = sortIntervalTicks;
        }
        return;
    }
    if (sortTrialTick++ >= sortTrialWarmupTicks) {
        sortTrialMsPerBall += kernelMs / static_cast<double>(count);
    }
    if (sortTrialTick < sortTrialWarmupTicks + sortTrialMeasuredTicks) {
        return;
    }
    sortCandidateCost[sortCandidate] = sortTrialMsPerBall / sortTrialMeasuredTicks;
    sortTrialTick = 0;
    sortTrialMsPerBall = 0.0;
    if (++sortCandidate < sortIntervalCandidates.size()) {
        sortIntervalTicks = sortIntervalCandidates[sortCandidate];
        ticksSinceSort = sortIntervalTicks;
        return;
    }
    const auto cheapest = std::ranges::min_element(sortCandidateCost);
    sortIntervalTicks = sortIntervalCandidates[static_cast<size_t>(cheapest - sortCandidateCost.begin())];
    ticksSinceSort = 0;
    ticksUntilSortTrial = ticksBetweenSortTrials;
}

bool CudaBackend::countBallsByCell() {
    const int columns = static_cast<int>(fieldWidth);
    const int rows = static_cast<int>(fieldHeight);
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

std::vector<BallSpawn> CudaBackend::exportBalls() {
    if (isFaulted() || count == 0) {
        return {};
    }
    driver.makeCurrent();
    std::vector<uint32_t> positions(count), states(count);
    const auto& api = driver.api();
    const bool downloaded =
        succeeded(api.memcpyDeviceToHost(positions.data(), balls.position, count * 4), "download position") &&
        succeeded(api.memcpyDeviceToHost(states.data(), balls.state, count * 4), "download state");
    if (!downloaded) {
        return {};
    }
    std::vector<BallSpawn> exported;
    exported.reserve(count);
    for (size_t i = 0; i < count; ++i) {
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
