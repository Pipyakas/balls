#include "rocm/RocmBackend.h"
#include "BallKernelImages_rocm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

constexpr unsigned int threadsPerBlock = 256;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr size_t capacityGranule = size_t{1} << 20;
constexpr size_t growthDivisor = 4;
constexpr size_t heldBytesPerBall = 8;
constexpr size_t deviceMemoryReserve = size_t{256} << 20;
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
constexpr size_t killedRecordWords = 2;
constexpr size_t sortBallsPerBlock = 4096;
constexpr unsigned int stepSortThreads = 256;
constexpr size_t ballsPerStepThread = 4;
constexpr size_t kernargRingInitialBytes = 64 * 1024;

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

uint32_t stateColor(uint32_t state) {
    return (state >> colorShift) & colorMask;
}

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

#pragma pack(push, 1)
struct StepBallsArgs {
    void* position;
    void* state;
    void* packed;
    int32_t count;
    int32_t pad0;
    const int32_t* cellWords;
    const int32_t* targetCells;
    StepParams params;
    int32_t pad1;
    int32_t* brickHitsLeft;
    uint32_t* killedIndices;
    uint32_t killedCapacity;
    uint32_t pad2;
    void* counters;
    void* gridSlots;
    int32_t gridWidth;
    int32_t gridHeight;
    float gridScaleX;
    float gridScaleY;
    uint32_t gridFrameSeed;
    uint32_t gridBidderMask;
    uint32_t packedCapacity;
};

struct StepAndSortBallsArgs {
    void* position;
    void* state;
    void* packed;
    int32_t count;
    int32_t pad0;
    const int32_t* cellWords;
    const int32_t* targetCells;
    StepParams params;
    int32_t pad1;
    int32_t* brickHitsLeft;
    uint32_t* killedIndices;
    uint32_t killedCapacity;
    uint32_t pad2;
    void* counters;
    void* gridSlots;
    int32_t gridWidth;
    int32_t gridHeight;
    float gridScaleX;
    float gridScaleY;
    uint32_t gridFrameSeed;
    uint32_t gridBidderMask;
    uint32_t packedCapacity;
    uint32_t pad3;
    void* cellCursors;
    void* sortedPosition;
    void* sortedState;
    int32_t columns;
    int32_t rows;
};

struct CountCellsArgs {
    const uint32_t* position;
    int32_t count;
    int32_t columns;
    int32_t rows;
    int32_t pad;
    uint32_t* cellCounts;
};

struct ScanCellsArgs {
    uint32_t* cellCounts;
    int32_t cellCount;
};

struct CollectHolesArgs {
    const uint32_t* killedIndices;
    uint32_t killedCount;
    uint32_t survivorCount;
    uint32_t* holes;
    void* counters;
};

struct FillHolesArgs {
    void* position;
    void* state;
    void* packed;
    uint32_t survivorCount;
    uint32_t oldCount;
    const uint32_t* holes;
    void* counters;
    uint32_t packedCapacity;
};

struct GridToRenderArgs {
    const void* slots;
    int32_t cells;
    int32_t pad;
    uint32_t* renderSlots;
};

struct ApplyCellWordsArgs {
    const int32_t* updates;
    int32_t updateCount;
    int32_t pad;
    int32_t* cellWords;
};

struct FreeFlightRenderArgs {
    void* position;
    void* state;
    void* packed;
    int32_t count;
    StepParams params;
    uint32_t ticksSinceEpoch;
    uint32_t pad;
    void* gridSlots;
    int32_t gridWidth;
    int32_t gridHeight;
    float gridScaleX;
    float gridScaleY;
    uint32_t gridFrameSeed;
    uint32_t gridBidderMask;
    uint32_t packedCapacity;
    uint32_t chunkStride;
    uint32_t chunkOffset;
};

struct MaterializeFreeFlightArgs {
    void* position;
    void* state;
    void* packed;
    int32_t count;
    StepParams params;
    uint32_t ticksSinceEpoch;
};
#pragma pack(pop)

}

std::string RocmBackend::unavailableReason(int deviceOrdinal) {
    const RocmDriver& driver = RocmDriver::forDevice(deviceOrdinal);
    return driver.isReady() ? std::string{} : driver.error();
}

size_t RocmBackend::streamedBytesPerBall(bool gridActive) {
    constexpr size_t readBytes = 2 * sizeof(uint32_t);
    constexpr size_t positionWriteBytes = sizeof(uint32_t);
    constexpr size_t packedWriteBytes = sizeof(uint32_t);
    return readBytes + positionWriteBytes + (gridActive ? 0 : packedWriteBytes);
}

RocmBackend::RocmBackend(int deviceOrdinal) : driver(RocmDriver::forDevice(deviceOrdinal)) {
    if (!driver.isReady()) {
        fault = driver.error();
        return;
    }
    const auto& api = driver.api();
    hsa_agent_t agent = driver.agent();

    uint32_t queueSize = 1024;
    api.agentGetInfo(agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &queueSize);
    if (queueSize > 1024) queueSize = 1024;
    if (!succeeded(api.queueCreate(agent, queueSize, HSA_QUEUE_TYPE_MULTI, nullptr, nullptr,
                                   UINT32_MAX, UINT32_MAX, &queue), "hsa_queue_create")) {
        return;
    }

    if (!succeeded(api.signalCreate(1, 0, nullptr, &stepSignal), "hsa_signal_create")) {
        return;
    }
    api.signalCreate(1, 0, nullptr, &countersSignal);
    if (driver.hostPool().handle) {
        api.amdMemoryPoolAllocate(driver.hostPool(), 256, 0, &hostCountersBuffer);
        hsa_agent_t agents[2] = {driver.agent(), driver.cpuAgent()};
        api.amdAgentsAllowAccess(2, agents, nullptr, hostCountersBuffer);
    }

    auto image = findRocmKernelImage(driver.modelName());
    hsa_code_object_reader_t reader{};
    if (!succeeded(api.codeObjectReaderCreateFromMemory(image.data(), image.size(), &reader),
                   "hsa_code_object_reader_create_from_memory")) {
        return;
    }
    if (!succeeded(api.executableCreateAlt(HSA_PROFILE_FULL, HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT,
                                           nullptr, &executable), "hsa_executable_create_alt")) {
        api.codeObjectReaderDestroy(reader);
        return;
    }
    if (!succeeded(api.executableLoadAgentCodeObject(executable, agent, reader, nullptr, nullptr),
                   "hsa_executable_load_agent_code_object") ||
        !succeeded(api.executableFreeze(executable, nullptr), "hsa_executable_freeze")) {
        api.codeObjectReaderDestroy(reader);
        return;
    }
    api.codeObjectReaderDestroy(reader);

    auto loadSym = [&](const char* name, KernelSymbol& sym) -> bool {
        std::string kdName = std::string(name) + ".kd";
        hsa_executable_symbol_t symbol{};
        if (api.executableGetSymbolByName(executable, kdName.c_str(), &agent, &symbol) != HSA_STATUS_SUCCESS &&
            api.executableGetSymbolByName(executable, name, &agent, &symbol) != HSA_STATUS_SUCCESS) {
            fault = std::string("kernel symbol not found: ") + name;
            return false;
        }
        api.executableSymbolGetInfo(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &sym.kernelObject);
        api.executableSymbolGetInfo(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &sym.kernargSize);
        api.executableSymbolGetInfo(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &sym.groupSize);
        api.executableSymbolGetInfo(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &sym.privateSize);
        return true;
    };

    if (!loadSym("stepBallsKernel", stepKernel) ||
        !loadSym("collectHolesKernel", collectHolesKernel) ||
        !loadSym("fillHolesKernel", fillHolesKernel) ||
        !loadSym("gridToRenderKernel", gridToRenderKernel) ||
        !loadSym("countCellsKernel", countCellsKernel) ||
        !loadSym("scanCellsKernel", scanCellsKernel) ||
        !loadSym("stepAndSortBallsKernel", stepAndSortKernel) ||
        !loadSym("applyCellWordsKernel", applyCellWordsKernel) ||
        !loadSym("freeFlightRenderKernel", freeFlightRenderKernel) ||
        !loadSym("materializeFreeFlightKernel", materializeFreeFlightKernel)) {
        return;
    }

    kernargRingSize = kernargRingInitialBytes;
    if (driver.kernargPool().handle != 0) {
        api.amdMemoryPoolAllocate(driver.kernargPool(), kernargRingSize, 0, &kernargRingBuffer);
        if (kernargRingBuffer) {
            hsa_agent_t allAgents[2] = {driver.agent(), driver.cpuAgent()};
            api.amdAgentsAllowAccess(2, allAgents, nullptr, kernargRingBuffer);
        }
    }

    ensureBallCapacity(initialBallCapacity);
    ensureArray(counters, sizeof(FrameCounters));
}

RocmBackend::~RocmBackend() {
    const auto& api = driver.api();
    if (stepSignal.handle != 0 && api.signalDestroy) {
        api.signalDestroy(stepSignal);
    }
    if (queue && api.queueDestroy) {
        api.queueDestroy(queue);
    }
    if (executable.handle != 0 && api.executableDestroy) {
        api.executableDestroy(executable);
    }
    if (kernargRingBuffer && api.amdMemoryPoolFree) {
        api.amdMemoryPoolFree(kernargRingBuffer);
    }
    if (hostCountersBuffer && api.amdMemoryPoolFree) {
        api.amdMemoryPoolFree(hostCountersBuffer);
        hostCountersBuffer = nullptr;
    }
    if (countersSignal.handle != 0 && api.signalDestroy) {
        api.signalDestroy(countersSignal);
        countersSignal = {};
    }
    if (stagingBuffer && api.amdMemoryPoolFree) {
        api.amdMemoryPoolFree(stagingBuffer);
    }
    for (void* ptr : {balls.position, balls.state, balls.packed,
                      paddedCells.pointer, brickColors.pointer, targetCells.pointer,
                      cellWords.pointer, brickHitsLeft.pointer, killedIndices.pointer,
                      holes.pointer, counters.pointer, gridSlots.pointer, gridPacked.pointer,
                      cellCursors.pointer, cellWordUpdates.pointer, sortedPosition, sortedState}) {
        if (ptr) {
            freeVram(ptr);
        }
    }
}

bool RocmBackend::succeeded(hsa_status_t status, const char* what) {
    if (status == HSA_STATUS_SUCCESS) return true;
    if (fault.empty()) {
        fault = driver.describe(status, what);
    }
    return false;
}

void* RocmBackend::allocateVram(size_t bytes) {
    if (bytes == 0) return nullptr;
    void* ptr = nullptr;
    const auto& api = driver.api();
    hsa_status_t status = api.amdMemoryPoolAllocate(driver.vramPool(), bytes, 0, &ptr);
    if (!succeeded(status, "allocateVram")) return nullptr;
    hsa_agent_t agent = driver.agent();
    api.amdAgentsAllowAccess(1, &agent, nullptr, ptr);
    return ptr;
}

void RocmBackend::freeVram(void* ptr) {
    if (ptr && driver.api().amdMemoryPoolFree) {
        driver.api().amdMemoryPoolFree(ptr);
    }
}

void RocmBackend::ensureArray(DeviceArray& array, size_t bytes) {
    if (isFaulted() || array.bytes >= bytes) return;
    if (array.pointer) {
        freeVram(array.pointer);
    }
    const size_t newBytes = std::max(bytes, array.bytes * 2);
    array.pointer = allocateVram(newBytes);
    array.bytes = array.pointer ? newBytes : 0;
}

void RocmBackend::ensureStagingCapacity(size_t bytes) {
    if (stagingCapacity >= bytes) return;
    const auto& api = driver.api();
    if (stagingBuffer && api.amdMemoryPoolFree) {
        api.amdMemoryPoolFree(stagingBuffer);
        stagingBuffer = nullptr;
        stagingCapacity = 0;
    }
    size_t newCapacity = std::max(bytes, stagingCapacity * 2);
    newCapacity = (newCapacity + capacityGranule - 1) / capacityGranule * capacityGranule;
    if (driver.hostPool().handle != 0) {
        if (api.amdMemoryPoolAllocate(driver.hostPool(), newCapacity, 0, &stagingBuffer) == HSA_STATUS_SUCCESS) {
            stagingCapacity = newCapacity;
            hsa_agent_t allAgents[2] = {driver.agent(), driver.cpuAgent()};
            api.amdAgentsAllowAccess(2, allAgents, nullptr, stagingBuffer);
        }
    }
}

void RocmBackend::uploadToDevice(void* devDst, const void* hostSrc, size_t bytes) {
    if (isFaulted() || bytes == 0 || !devDst || !hostSrc) return;
    ensureStagingCapacity(bytes);
    if (!stagingBuffer) return;
    std::memcpy(stagingBuffer, hostSrc, bytes);
    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    succeeded(api.amdMemoryAsyncCopy(devDst, driver.agent(), stagingBuffer, driver.cpuAgent(),
                                     bytes, 0, nullptr, stepSignal), "async copy HtoD");
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
}

void RocmBackend::downloadFromDevice(void* hostDst, const void* devSrc, size_t bytes) {
    if (isFaulted() || bytes == 0 || !hostDst || !devSrc) return;
    ensureStagingCapacity(bytes);
    if (!stagingBuffer) return;
    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    succeeded(api.amdMemoryAsyncCopy(stagingBuffer, driver.cpuAgent(), devSrc, driver.agent(),
                                     bytes, 0, nullptr, stepSignal), "async copy DtoH");
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
    std::memcpy(hostDst, stagingBuffer, bytes);
}

void RocmBackend::upload(DeviceArray& array, const void* data, size_t bytes) {
    ensureArray(array, bytes);
    uploadToDevice(array.pointer, data, bytes);
}

void RocmBackend::download(void* dest, const void* src, size_t bytes) {
    downloadFromDevice(dest, src, bytes);
}

void RocmBackend::copyDeviceToDevice(void* dst, const void* src, size_t bytes) {
    if (isFaulted() || bytes == 0) return;
    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    succeeded(api.amdMemoryAsyncCopy(dst, driver.agent(), src, driver.agent(),
                                     bytes, 0, nullptr, stepSignal), "async copy DtoD");
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
}

void RocmBackend::fillDeviceMemory(void* ptr, uint32_t value, size_t dwords) {
    if (isFaulted() || dwords == 0 || !ptr) return;
    succeeded(driver.api().amdMemoryFill(ptr, value, dwords), "hsa_amd_memory_fill");
}

void* RocmBackend::getKernargSlot(size_t size) {
    size_t aligned = (size + 15) & ~size_t{15};
    if (kernargRingOffset + aligned > kernargRingSize) {
        kernargRingOffset = 0;
    }
    void* slot = static_cast<char*>(kernargRingBuffer) + kernargRingOffset;
    kernargRingOffset += aligned;
    return slot;
}

void RocmBackend::resetKernargRing() {
    kernargRingOffset = 0;
}

uint64_t RocmBackend::queuePacket(const KernelSymbol& kernel, uint32_t workgroupSize,
                                  uint32_t gridSize, const void* args, size_t argsSize,
                                  bool barrier, hsa_signal_t signal) {
    void* kernargs = getKernargSlot(argsSize);
    std::memcpy(kernargs, args, argsSize);

    const auto& api = driver.api();
    uint64_t index = api.queueAddWriteIndexRelaxed(queue, 1);
    const uint32_t mask = queue->size - 1;
    auto* packet = static_cast<hsa_kernel_dispatch_packet_t*>(queue->base_address) + (index & mask);

    std::memset(packet, 0, sizeof(*packet));
    packet->setup = 1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
    packet->workgroup_size_x = workgroupSize;
    packet->workgroup_size_y = 1;
    packet->workgroup_size_z = 1;
    packet->grid_size_x = gridSize;
    packet->grid_size_y = 1;
    packet->grid_size_z = 1;
    packet->kernel_object = kernel.kernelObject;
    packet->kernarg_address = kernargs;
    packet->group_segment_size = kernel.groupSize;
    packet->private_segment_size = kernel.privateSize;
    packet->completion_signal = signal;

    uint16_t header = (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
                      (barrier ? (1 << HSA_PACKET_HEADER_BARRIER) : 0) |
                      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE) |
                      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);
    __atomic_store_n(reinterpret_cast<uint16_t*>(packet), header | (packet->setup << 16), __ATOMIC_RELEASE);
    return index;
}

bool RocmBackend::launch(const KernelSymbol& kernel, uint32_t workgroupSize,
                         uint32_t totalThreads, const void* args, size_t argsSize, const char* what) {
    (void)what;
    if (totalThreads == 0 || isFaulted()) return true;
    uint32_t blocks = (totalThreads + workgroupSize - 1) / workgroupSize;
    uint32_t gridSize = blocks * workgroupSize;
    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    uint64_t index = queuePacket(kernel, workgroupSize, gridSize, args, argsSize, true, stepSignal);
    api.signalStoreScrelease(queue->doorbell_signal, index);
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
    return !isFaulted();
}

size_t RocmBackend::ballCapacityLimit() const {
    if (isFaulted()) return count;
    size_t totalBytes = driver.totalMemoryBytes();
    size_t spare = totalBytes > deviceMemoryReserve ? totalBytes - deviceMemoryReserve : 0;
    return capacity + spare / heldBytesPerBall;
}

bool RocmBackend::ensureBallCapacity(size_t required) {
    if (isFaulted()) return false;
    if (required <= capacity) return true;
    releaseSortBuffers();
    const auto roundUp = [](size_t balls) { return (balls + capacityGranule - 1) / capacityGranule * capacityGranule; };
    for (const size_t wanted : {roundUp(std::max(required, capacity + capacity / growthDivisor)), roundUp(required)}) {
        void* grownPosition = allocateVram(wanted * 4);
        if (!grownPosition) continue;
        void* grownState = allocateVram(wanted * 4);
        if (!grownState) {
            freeVram(grownPosition);
            continue;
        }
        if (count > 0) {
            copyDeviceToDevice(grownPosition, balls.position, count * 4);
            copyDeviceToDevice(grownState, balls.state, count * 4);
        }
        if (balls.position) freeVram(balls.position);
        if (balls.state) freeVram(balls.state);
        balls.position = grownPosition;
        balls.state = grownState;
        capacity = wanted;
        return true;
    }
    return growThroughHost(roundUp(required));
}

bool RocmBackend::growThroughHost(size_t wanted) {
    std::vector<uint32_t> heldPosition(count);
    std::vector<uint32_t> heldState(count);
    if (count > 0) {
        downloadFromDevice(heldPosition.data(), balls.position, count * 4);
        downloadFromDevice(heldState.data(), balls.state, count * 4);
    }
    if (balls.position) freeVram(balls.position);
    if (balls.state) freeVram(balls.state);
    balls.position = nullptr;
    balls.state = nullptr;
    const size_t previousCapacity = capacity;
    capacity = 0;
    bool grown = false;
    for (const size_t attempt : {wanted, previousCapacity}) {
        void* position = allocateVram(attempt * 4);
        if (!position) continue;
        void* state = allocateVram(attempt * 4);
        if (!state) {
            freeVram(position);
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
        uploadToDevice(balls.position, heldPosition.data(), count * 4);
        uploadToDevice(balls.state, heldState.data(), count * 4);
    }
    return grown;
}

void RocmBackend::ensurePackedCapacity(size_t required) {
    if (isFaulted() || required <= packedCapacity) return;
    const size_t newCapacity = (required + capacityGranule - 1) / capacityGranule * capacityGranule;
    if (balls.packed) {
        freeVram(balls.packed);
        balls.packed = nullptr;
    }
    packedCapacity = 0;
    balls.packed = allocateVram(newCapacity * 4);
    if (balls.packed) {
        packedCapacity = newCapacity;
    }
}

void RocmBackend::uploadBoard(const BoardUpload& board) {
    if (isFaulted()) return;
    if (board.onlyChangedCells && uploadedCellWords == board.paddedCells.size()) {
        applyChangedCellWords(board);
    } else {
        uploadWholeBoard(board);
    }
    if (!board.targetCells.empty()) {
        upload(targetCells, board.targetCells.data(), board.targetCells.size_bytes());
    }
    if (!board.brickHitsLeft.empty()) {
        uploadBrickHits(board.brickHitsLeft);
    }
    fieldWidth = static_cast<float>(board.columns);
    fieldHeight = static_cast<float>(board.rows);
    chooseSortGrid();
}

void RocmBackend::applyChangedCellWords(const BoardUpload& board) {
    if (board.changedCells.empty()) return;
    const std::vector<int32_t> updates = buildCellWordUpdates(board);
    upload(cellWordUpdates, updates.data(), updates.size() * sizeof(int32_t));
    ApplyCellWordsArgs args{
        static_cast<const int32_t*>(cellWordUpdates.pointer),
        static_cast<int32_t>(board.changedCells.size()),
        0,
        static_cast<int32_t*>(cellWords.pointer)
    };
    launch(applyCellWordsKernel, threadsPerBlock, board.changedCells.size(), &args, sizeof(args), "applyCellWordsKernel");
}

void RocmBackend::uploadWholeBoard(const BoardUpload& board) {
    if (board.paddedCells.empty()) return;
    upload(paddedCells, board.paddedCells.data(), board.paddedCells.size_bytes());
    std::vector<int32_t> colors(board.brickColors.begin(), board.brickColors.end());
    colors.push_back(-1);
    upload(brickColors, colors.data(), colors.size() * sizeof(int32_t));
    const std::vector<int32_t> words = buildCellWords(board);
    upload(cellWords, words.data(), words.size() * sizeof(int32_t));
    uploadedCellWords = words.size();
}

void RocmBackend::uploadBrickHits(std::span<const int32_t> hitsLeft) {
    if (isFaulted()) return;
    hostBrickHitsLeft.assign(hitsLeft.begin(), hitsLeft.end());
    brickCount = hostBrickHitsLeft.size();
    hostBrickHitsLeft.push_back(0);
    upload(brickHitsLeft, hostBrickHitsLeft.data(), hostBrickHitsLeft.size() * sizeof(int32_t));
    hostBrickHitsLeft.pop_back();
    BallCount totalHits = 0;
    for (int32_t hits : hostBrickHitsLeft) {
        totalHits += static_cast<BallCount>(std::max(hits, 0));
    }
    killedCapacity = static_cast<uint32_t>(std::max<BallCount>(totalHits, minimumKilledCapacity));
    ensureArray(killedIndices, static_cast<size_t>(killedCapacity) * killedRecordWords * 4);
    ensureArray(holes, static_cast<size_t>(killedCapacity) * 4);
}

void RocmBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty() || isFaulted()) return;
    if (!ensureBallCapacity(count + spawns.size()) && isFaulted()) return;
    const size_t added = std::min(spawns.size(), capacity - count);
    std::vector<uint32_t> position(added), state(added), packed(added);
    ballSpeed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeedScale = 1.0f / ballSpeed;
    const unsigned int ticksSinceEpoch = clockTick - freeFlightEpoch;
    for (size_t i = 0; i < added; ++i) {
        const BallSpawn& spawn = spawns[i];
        const uint32_t color = static_cast<uint32_t>(spawn.color);
        int directionX = signedComponent(encodeComponent(spawn.vx * inverseSpeedScale));
        int directionY = signedComponent(encodeComponent(spawn.vy * inverseSpeedScale));
        float x = spawn.x;
        float y = spawn.y;
        if (frozen) {
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
    const size_t offset = count * 4;
    uploadToDevice(static_cast<char*>(balls.position) + offset, position.data(), added * 4);
    uploadToDevice(static_cast<char*>(balls.state) + offset, state.data(), added * 4);
    if (count + added <= packedCapacity && balls.packed) {
        uploadToDevice(static_cast<char*>(balls.packed) + offset, packed.data(), added * 4);
    }
    count += added;
    sortSchedule.ballsAdded(added, count);
    snapshotFresh = false;
}

bool RocmBackend::prepareVisibilityGrid(DeviceVisibilityGridArgument& grid) {
    const size_t cells = gridCellCount();
    if (cells == 0 || count <= cells) return false;
    ensureArray(gridSlots, cells * sizeof(unsigned long long));
    if (isFaulted()) return false;
    fillDeviceMemory(gridSlots.pointer, 0, cells * 2);
    const float coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateMax + 1);
    const float coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateMax + 1);
    grid = {gridSlots.pointer, gridWidth, gridHeight, coordinateToSlotX, coordinateToSlotY, ++frameSeed,
            bidderMaskFor(count, cells)};
    gridFilled = true;
    return true;
}

void RocmBackend::renderFreeFlight(const StepParams& params) {
    if (!frozen) {
        frozen = true;
        freeFlightEpoch = params.tick;
        releaseSortBuffers();
    }
    resetKernargRing();
    DeviceVisibilityGridArgument grid{};
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
    if (sampledChunks == 0) return;

    FreeFlightRenderArgs args{
        balls.position, balls.state, balls.packed,
        static_cast<int32_t>(count),
        params,
        clockTick - freeFlightEpoch,
        0,
        grid.slots, grid.width, grid.height, grid.scaleX, grid.scaleY, grid.frameSeed, grid.bidderMask,
        static_cast<uint32_t>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu)),
        chunkStride, chunkOffset
    };

    auto start = std::chrono::high_resolution_clock::now();
    launch(freeFlightRenderKernel, threadsPerBlock, sampledChunks * threadsPerBlock, &args, sizeof(args), "freeFlightRenderKernel");
    auto end = std::chrono::high_resolution_clock::now();
    lastStepKernelMs = std::chrono::duration<double, std::milli>(end - start).count();
}

void RocmBackend::materializeFreeFlight() {
    if (!frozen) return;
    frozen = false;
    if (count == 0 || isFaulted()) return;
    MaterializeFreeFlightArgs args{
        balls.position, balls.state, balls.packed,
        static_cast<int32_t>(count),
        lastParams,
        clockTick - freeFlightEpoch
    };
    launch(materializeFreeFlightKernel, threadsPerBlock, count, &args, sizeof(args), "materializeFreeFlightKernel");
}

size_t RocmBackend::gridCellCount() const {
    return static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight);
}

void RocmBackend::setVisibilityGrid(int width, int height) {
    gridWidth = std::max(width, 0);
    gridHeight = std::max(height, 0);
}

void RocmBackend::step(const StepParams& params, std::vector<BallHit>&) {
    snapshotFresh = false;
    gridFilled = false;
    lastCounters = {};
    if (count == 0 || isFaulted()) return;

    lastParams = params;
    clockTick = params.tick + static_cast<unsigned int>(params.substeps);
    if (params.freeFlight) {
        renderFreeFlight(params);
        return;
    }
    materializeFreeFlight();

    if (countersDirty) {
        fillDeviceMemory(counters.pointer, 0, sizeof(FrameCounters) / 4);
        countersDirty = false;
    }

    DeviceVisibilityGridArgument grid{};
    if (!prepareVisibilityGrid(grid)) {
        ensurePackedCapacity(count);
    }

    resetKernargRing();

    bool sorting = false;
    DeviceSortedOutput sorted{};
    if (sortSchedule.sortThisTick(count)) {
        if (countBallsByCell()) {
            sorting = true;
            sorted = {cellCursors.pointer, sortedPosition, sortedState, sortColumns, sortRows};
        }
    }

    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    uint64_t lastIndex = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    if (sorting) {

        StepAndSortBallsArgs sortArgs{
            balls.position, balls.state, balls.packed,
            static_cast<int32_t>(count),
            0,
            static_cast<const int32_t*>(cellWords.pointer),
            static_cast<const int32_t*>(targetCells.pointer),
            params,
            0,
            static_cast<int32_t*>(brickHitsLeft.pointer),
            static_cast<uint32_t*>(killedIndices.pointer),
            killedCapacity,
            0,
            counters.pointer,
            grid.slots, grid.width, grid.height, grid.scaleX, grid.scaleY, grid.frameSeed, grid.bidderMask,
            static_cast<uint32_t>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu)),
            0,
            sorted.cellCursors, sorted.position, sorted.state, sorted.columns, sorted.rows
        };
        uint32_t sortBlocks = static_cast<uint32_t>((count + sortBallsPerBlock - 1) / sortBallsPerBlock);
        lastIndex = queuePacket(stepAndSortKernel, stepSortThreads, sortBlocks * stepSortThreads,
                                &sortArgs, sizeof(sortArgs), true, stepSignal);
    } else {

        StepBallsArgs stepArgs{
            balls.position, balls.state, balls.packed,
            static_cast<int32_t>(count),
            0,
            static_cast<const int32_t*>(cellWords.pointer),
            static_cast<const int32_t*>(targetCells.pointer),
            params,
            0,
            static_cast<int32_t*>(brickHitsLeft.pointer),
            static_cast<uint32_t*>(killedIndices.pointer),
            killedCapacity,
            0,
            counters.pointer,
            grid.slots, grid.width, grid.height, grid.scaleX, grid.scaleY, grid.frameSeed, grid.bidderMask,
            static_cast<uint32_t>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu))
        };
        uint32_t threadsNeeded = static_cast<uint32_t>((count + ballsPerStepThread - 1) / ballsPerStepThread);
        uint32_t blocks = (threadsNeeded + threadsPerBlock - 1) / threadsPerBlock;
        lastIndex = queuePacket(stepKernel, threadsPerBlock, blocks * threadsPerBlock,
                                &stepArgs, sizeof(stepArgs), false, stepSignal);
    }

    api.signalStoreScrelease(queue->doorbell_signal, lastIndex);
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    auto endTime = std::chrono::high_resolution_clock::now();
    lastStepKernelMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    if (sorting) {
        std::swap(balls.position, sortedPosition);
        std::swap(balls.state, sortedState);
    }
    sortSchedule.record(static_cast<float>(lastStepKernelMs), count);

    if (hostCountersBuffer && countersSignal.handle) {
        api.signalStoreRelaxed(countersSignal, 1);
        api.amdMemoryAsyncCopy(hostCountersBuffer, driver.cpuAgent(), counters.pointer, driver.agent(), sizeof(FrameCounters), 0, nullptr, countersSignal);
        api.signalWaitScacquire(countersSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
        std::memcpy(&lastCounters, hostCountersBuffer, sizeof(FrameCounters));
    } else {
        downloadFromDevice(&lastCounters, counters.pointer, sizeof(FrameCounters));
    }
    lastCounters.killedCount = std::min(lastCounters.killedCount, killedCapacity);
    if (lastCounters.killedCount == 0) return;
    countersDirty = true;

    compactKilledBalls(lastCounters.killedCount);
    hostHitBricks.resize(lastCounters.killedCount);
    downloadFromDevice(hostHitBricks.data(),
                       static_cast<char*>(killedIndices.pointer) + static_cast<size_t>(killedCapacity) * 4,
                       hostHitBricks.size() * sizeof(uint32_t));
}

void RocmBackend::compactKilledBalls(uint32_t killed) {
    const uint32_t oldCount = static_cast<uint32_t>(count);
    uint32_t survivorCount = oldCount - killed;

    CollectHolesArgs collectArgs{
        static_cast<const uint32_t*>(killedIndices.pointer),
        killed,
        survivorCount,
        static_cast<uint32_t*>(holes.pointer),
        counters.pointer
    };
    launch(collectHolesKernel, threadsPerBlock, killed, &collectArgs, sizeof(collectArgs), "collectHolesKernel");

    FillHolesArgs fillArgs{
        balls.position, balls.state, balls.packed,
        survivorCount,
        oldCount,
        static_cast<const uint32_t*>(holes.pointer),
        counters.pointer,
        static_cast<uint32_t>(std::min<size_t>(packedCapacity, 0xFFFFFFFFu))
    };
    launch(fillHolesKernel, threadsPerBlock, killed, &fillArgs, sizeof(fillArgs), "fillHolesKernel");

    count = survivorCount;
}

DeviceHitResults RocmBackend::deviceHitResults() const {
    DeviceHitResults results;
    if (lastCounters.killedCount == 0) return results;
    results.hitBricks = hostHitBricks;
    for (int color = 0; color < ballColorCount; ++color) {
        results.killsByColor[color] = lastCounters.killsByColor[color];
    }
    results.killed = lastCounters.killedCount;
    return results;
}

void RocmBackend::removeBalls(std::span<const uint32_t>) {}

BallTally RocmBackend::truncate(size_t newCount) {
    if (isFaulted() || newCount >= count) return {};
    const BallTally removed = downloadColorTally(newCount, count);
    count = newCount;
    snapshotFresh = false;
    return removed;
}

BallTally RocmBackend::downloadColorTally(size_t first, size_t last) {
    BallTally tally{};
    if (first >= last) return tally;
    std::vector<uint32_t> states(last - first);
    downloadFromDevice(states.data(), static_cast<char*>(balls.state) + first * 4, states.size() * 4);
    for (uint32_t value : states) {
        const uint32_t color = stateColor(value);
        if (color != deadColor) {
            ++tally[color];
        }
    }
    return tally;
}

void RocmBackend::downloadPacked() {
    hostPacked.resize(std::min(count, packedCapacity));
    if (!hostPacked.empty() && balls.packed) {
        downloadFromDevice(hostPacked.data(), balls.packed, hostPacked.size() * 4);
    }
}

bool RocmBackend::downloadGrid() {
    const size_t cells = gridCellCount();
    ensureArray(gridPacked, cells * 4);
    if (isFaulted()) return false;
    GridToRenderArgs args{
        gridSlots.pointer,
        static_cast<int32_t>(cells),
        0,
        static_cast<uint32_t*>(gridPacked.pointer)
    };
    if (!launch(gridToRenderKernel, threadsPerBlock, cells, &args, sizeof(args), "gridToRenderKernel")) {
        return false;
    }
    hostPacked.resize(cells);
    downloadFromDevice(hostPacked.data(), gridPacked.pointer, cells * 4);
    return !isFaulted();
}

void RocmBackend::releaseSortBuffers() {
    for (void** buffer : {&sortedPosition, &sortedState}) {
        if (*buffer) {
            freeVram(*buffer);
            *buffer = nullptr;
        }
    }
    sortedCapacity = 0;
}

bool RocmBackend::ensureSortBuffers() {
    if (sortedCapacity == capacity && sortedPosition && sortedState) return true;
    releaseSortBuffers();
    sortedPosition = allocateVram(capacity * 4);
    sortedState = allocateVram(capacity * 4);
    if (!sortedPosition || !sortedState) {
        releaseSortBuffers();
        return false;
    }
    sortedCapacity = capacity;
    return true;
}

void RocmBackend::chooseSortGrid() {
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

bool RocmBackend::countBallsByCell() {
    const int columns = sortColumns;
    const int rows = sortRows;
    const int cellCount = columns * rows;
    if (count == 0 || cellCount <= 0 || cellCount > maxSortCells || !ensureSortBuffers()) {
        return false;
    }
    ensureArray(cellCursors, static_cast<size_t>(cellCount) * 4);
    if (isFaulted()) return false;
    fillDeviceMemory(cellCursors.pointer, 0, cellCount);

    const size_t countThreads = (count + sortBallsPerBlock - 1) / sortBallsPerBlock * threadsPerBlock;
    CountCellsArgs countArgs{
        static_cast<const uint32_t*>(balls.position),
        static_cast<int32_t>(count),
        columns,
        rows,
        0,
        static_cast<uint32_t*>(cellCursors.pointer)
    };
    ScanCellsArgs scanArgs{
        static_cast<uint32_t*>(cellCursors.pointer),
        cellCount
    };

    const auto& api = driver.api();
    api.signalStoreRelaxed(stepSignal, 1);
    uint32_t countBlocks = static_cast<uint32_t>((countThreads + threadsPerBlock - 1) / threadsPerBlock);
    queuePacket(countCellsKernel, threadsPerBlock, countBlocks * threadsPerBlock,
                &countArgs, sizeof(countArgs), false, hsa_signal_t{0});
    uint64_t lastIdx = queuePacket(scanCellsKernel, threadsPerBlock, threadsPerBlock,
                                   &scanArgs, sizeof(scanArgs), true, stepSignal);
    api.signalStoreScrelease(queue->doorbell_signal, lastIdx);
    api.signalWaitScacquire(stepSignal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
    return !isFaulted();
}

BallSnapshot RocmBackend::snapshot() {
    if (isFaulted()) return {};
    if (gridFilled) {
        if (!snapshotFresh && downloadGrid()) {
            snapshotFresh = true;
        }
        return snapshotFresh ? BallSnapshot{hostPacked.data(), hostPacked.size(), fieldWidth, fieldHeight, false, true}
                             : BallSnapshot{};
    }
    if (!snapshotFresh) {
        downloadPacked();
        snapshotFresh = true;
    }
    if (isFaulted()) return {};
    return {hostPacked.data(), hostPacked.size(), fieldWidth, fieldHeight, false};
}

std::vector<BallSpawn> RocmBackend::exportBalls() {
    return exportRange(0);
}

std::vector<BallSpawn> RocmBackend::takeTail(size_t tailCount) {
    const size_t first = count - std::min(tailCount, count);
    std::vector<BallSpawn> tail = exportRange(first);
    if (!isFaulted()) {
        count = first;
        snapshotFresh = false;
    }
    return tail;
}

bool RocmBackend::takeCompactTail(size_t tailCount, CompactBalls& taken) {
    materializeFreeFlight();
    if (isFaulted()) return false;
    const size_t first = count - std::min(tailCount, count);
    const size_t takenCount = count - first;
    taken.position.resize(takenCount);
    taken.state.resize(takenCount);
    if (takenCount > 0) {
        downloadFromDevice(taken.position.data(), static_cast<char*>(balls.position) + first * 4, takenCount * 4);
        downloadFromDevice(taken.state.data(), static_cast<char*>(balls.state) + first * 4, takenCount * 4);
    }
    if (isFaulted()) return false;
    count = first;
    snapshotFresh = false;
    return true;
}

bool RocmBackend::addCompact(const CompactBalls& added) {
    materializeFreeFlight();
    const size_t addedCount = std::min(added.position.size(), added.state.size());
    if (isFaulted()) return false;
    if (addedCount == 0) return true;
    if (!ensureBallCapacity(count + addedCount)) return false;
    const size_t offset = count * 4;
    uploadToDevice(static_cast<char*>(balls.position) + offset, added.position.data(), addedCount * 4);
    uploadToDevice(static_cast<char*>(balls.state) + offset, added.state.data(), addedCount * 4);
    if (count + addedCount <= packedCapacity && balls.packed) {
        std::vector<uint32_t> packed(addedCount);
        for (size_t i = 0; i < addedCount; ++i) {
            packed[i] = packCompactForRender(added.position[i], added.state[i]);
        }
        uploadToDevice(static_cast<char*>(balls.packed) + offset, packed.data(), addedCount * 4);
    }
    count += addedCount;
    sortSchedule.ballsAdded(addedCount, count);
    snapshotFresh = false;
    return !isFaulted();
}

std::vector<BallSpawn> RocmBackend::exportRange(size_t first) {
    materializeFreeFlight();
    if (isFaulted() || first >= count) return {};
    const size_t rangeCount = count - first;
    std::vector<uint32_t> positions(rangeCount), states(rangeCount);
    downloadFromDevice(positions.data(), static_cast<char*>(balls.position) + first * 4, rangeCount * 4);
    downloadFromDevice(states.data(), static_cast<char*>(balls.state) + first * 4, rangeCount * 4);
    if (isFaulted()) return {};
    std::vector<BallSpawn> exported;
    exported.reserve(rangeCount);
    for (size_t i = 0; i < rangeCount; ++i) {
        const uint32_t color = stateColor(states[i]);
        if (color == deadColor) continue;
        const float dx = static_cast<float>(signedComponent(states[i] & directionMask));
        const float dy = static_cast<float>(signedComponent((states[i] >> directionBits) & directionMask));
        const float length = std::max(std::hypot(dx, dy), 1.0f);
        exported.push_back({
            static_cast<float>(positions[i] & positionMask) * fieldWidth / positionScale,
            static_cast<float>(positions[i] >> 16) * fieldHeight / positionScale,
            dx / length * ballSpeed, dy / length * ballSpeed,
            static_cast<int32_t>(color)
        });
    }
    return exported;
}
