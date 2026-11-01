#include "vulkan/VulkanBackend.h"

#include "BallStepVariants.h"
#include "generated/CompactionBlobs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <utility>

namespace {

constexpr size_t initialBallCapacity = size_t{1} << 16;
constexpr size_t capacityGranule = size_t{1} << 20;
constexpr size_t capacityGrowthDivisor = 4;
constexpr size_t maximumSortCells = 4096;
constexpr uint32_t maximumGroupsPerRow = 32768;
constexpr uint32_t counterWordCount = 10;
constexpr uint32_t minimumKilledCapacity = 1024;
constexpr uint32_t colorShift = 26;
constexpr uint32_t directionBits = 13;
constexpr uint32_t directionMask = (1u << directionBits) - 1u;
constexpr uint32_t deadColor = 7u;
constexpr float directionScale = 4095.0f;
constexpr float positionScale = 65535.0f;

struct DispatchShape {
    uint32_t groupsPerRow = 1;
    uint32_t rows = 1;
};

DispatchShape shapeFor(size_t items, size_t itemsPerGroup) {
    const size_t groups = (items + itemsPerGroup - 1) / itemsPerGroup;
    const uint32_t perRow = static_cast<uint32_t>(std::max<size_t>(std::min<size_t>(groups, maximumGroupsPerRow), 1));
    return {perRow, static_cast<uint32_t>((groups + perRow - 1) / perRow)};
}

uint32_t encodeAxis(float value, float extent) {
    const float scaled = value / extent * positionScale + 0.5f;
    return static_cast<uint32_t>(std::clamp(scaled, 0.0f, positionScale));
}

uint32_t encodeComponent(float value) {
    const int quantized = static_cast<int>(std::lrint(std::clamp(value, -1.0f, 1.0f) * directionScale));
    return static_cast<uint32_t>(quantized) & directionMask;
}

int32_t signedComponent(uint32_t bits) {
    return static_cast<int32_t>(bits << (32 - directionBits)) >> (32 - directionBits);
}

uint32_t stateColor(uint32_t state) { return (state >> colorShift) & 7u; }

std::vector<int32_t> buildCellWords(const BoardUpload& board) {
    constexpr int32_t emptyFarCell = -1;
    constexpr int32_t cellBrickShift = 3;
    std::vector<int32_t> words(board.paddedCells.size(), emptyFarCell);
    for (size_t cell = 0; cell < words.size(); ++cell) {
        const int32_t brick = board.paddedCells[cell];
        if (brick >= 0) {
            words[cell] = (brick << cellBrickShift) | board.brickColors[static_cast<size_t>(brick)];
        } else if (board.nearBrickCells[cell] != 0) {
            words[cell] = -2;
        }
    }
    return words;
}

template <size_t Size>
std::span<const unsigned char> shaderSpan(const unsigned char (&data)[Size]) {
    return {data, Size};
}

}

VulkanBackend::VulkanBackend(std::shared_ptr<VulkanComputeDevice> selectedDevice)
    : computeDevice(std::move(selectedDevice)), device(computeDevice ? computeDevice->device() : nullptr),
      tuning(computeDevice ? computeDevice->tuning() : BallStepTuning{}) {
    if (!device) {
        setFault("Vulkan compute device is unavailable");
        return;
    }
    initialize();
}

void VulkanBackend::setFault(std::string message) {
    if (fault.empty()) {
        fault = std::move(message);
    }
}

nvrhi::BufferHandle VulkanBackend::structuredBuffer(size_t elements, bool writable, const char* name) {
    nvrhi::BufferDesc description;
    description.byteSize = std::max<size_t>(elements, 1) * sizeof(uint32_t);
    description.structStride = sizeof(uint32_t);
    description.canHaveUAVs = writable;
    description.initialState = writable ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;
    description.keepInitialState = true;
    description.debugName = name;
    return device->createBuffer(description);
}

nvrhi::BufferHandle VulkanBackend::constantBuffer(size_t bytes, const char* name) {
    nvrhi::BufferDesc description;
    description.byteSize = bytes;
    description.isConstantBuffer = true;
    description.initialState = nvrhi::ResourceStates::ConstantBuffer;
    description.keepInitialState = true;
    description.debugName = name;
    return device->createBuffer(description);
}

void VulkanBackend::ensureReadback(nvrhi::BufferHandle& buffer, size_t bytes, const char* name) {
    if (isFaulted() || (buffer && buffer->getDesc().byteSize >= bytes)) {
        return;
    }
    nvrhi::BufferDesc description;
    description.byteSize = std::max<size_t>(bytes, sizeof(uint32_t));
    description.cpuAccess = nvrhi::CpuAccessMode::Read;
    description.initialState = nvrhi::ResourceStates::CopyDest;
    description.keepInitialState = true;
    description.debugName = name;
    buffer = device->createBuffer(description);
    if (!buffer) {
        setFault(std::format("failed to allocate Vulkan {} readback buffer", name));
    }
}

bool VulkanBackend::initialize() {
    static_assert(sizeof(ShaderStepParams) == 44);
    static_assert(sizeof(StepConstants) == 80);
    static_assert(sizeof(CompactionConstants) == 16);

    cellWords = structuredBuffer(1, false, "cellWords");
    targetCells = structuredBuffer(1, false, "targetCells");
    brickHitsLeft = structuredBuffer(1, true, "brickHitsLeft");
    killedIndices = structuredBuffer(killedCapacity, true, "killedIndices");
    holes = structuredBuffer(killedCapacity, true, "holes");
    counters = structuredBuffer(counterWordCount, true, "frameCounters");
    cellCursors = structuredBuffer(maximumSortCells, true, "cellCursors");
    stepConstants = constantBuffer(sizeof(StepConstants), "stepConstants");
    countConstants = constantBuffer(sizeof(StepConstants), "countConstants");
    sortConstants = constantBuffer(sizeof(StepConstants), "sortConstants");
    compactionConstants = constantBuffer(sizeof(CompactionConstants), "compactionConstants");
    ensureReadback(countersReadback, counterWordCount * sizeof(uint32_t), "counter");
    ensureReadback(hitsReadback, sizeof(uint32_t), "brick hit");
    if (!cellWords || !targetCells || !brickHitsLeft || !killedIndices || !holes || !counters || !cellCursors ||
        !stepConstants || !countConstants || !sortConstants || !compactionConstants || isFaulted()) {
        setFault("failed to allocate Vulkan physics buffers");
        return false;
    }
    ensureBallCapacity(initialBallCapacity);
    if (isFaulted()) {
        return false;
    }

    nvrhi::BindingLayoutDesc stepLayoutDescription;
    stepLayoutDescription.visibility = nvrhi::ShaderType::Compute;
    stepLayoutDescription.bindings = {nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),
                                      nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8),
                                      nvrhi::BindingLayoutItem::ConstantBuffer(0)};
    stepLayout = device->createBindingLayout(stepLayoutDescription);

    nvrhi::BindingLayoutDesc compactionLayoutDescription;
    compactionLayoutDescription.visibility = nvrhi::ShaderType::Compute;
    compactionLayoutDescription.bindings = {nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                                            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
                                            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
                                            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
                                            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
                                            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),
                                            nvrhi::BindingLayoutItem::ConstantBuffer(0)};
    compactionLayout = device->createBindingLayout(compactionLayoutDescription);

    const BallStepBlobSet shaders = ballStepSpirvSet(tuning.variant);
    auto makePipeline = [&](std::span<const unsigned char> blob, const char* entry,
                            nvrhi::IBindingLayout* layout) -> nvrhi::ComputePipelineHandle {
        nvrhi::ShaderDesc shaderDescription;
        shaderDescription.shaderType = nvrhi::ShaderType::Compute;
        shaderDescription.entryName = entry;
        nvrhi::ShaderHandle shader = device->createShader(shaderDescription, blob.data(), blob.size());
        if (!shader) {
            setFault(std::format("failed to create Vulkan {} compute shader", entry));
            return nullptr;
        }
        nvrhi::ComputePipelineDesc pipelineDescription;
        pipelineDescription.CS = shader;
        pipelineDescription.bindingLayouts = {layout};
        nvrhi::ComputePipelineHandle pipeline = device->createComputePipeline(pipelineDescription);
        if (!pipeline) {
            setFault(std::format("failed to create Vulkan {} compute pipeline", entry));
        }
        return pipeline;
    };
    stepPipeline = makePipeline(shaders.ballStep, "computeMain", stepLayout);
    countPipeline = makePipeline(shaders.countCells, "computeMain", stepLayout);
    scanPipeline = makePipeline(shaders.scanCells, "computeMain", stepLayout);
    sortPipeline = makePipeline(shaders.stepAndSort, "computeMain", stepLayout);
    collectHolesPipeline = makePipeline(shaderSpan(collectHolesSpirv), "collectHoles", compactionLayout);
    fillHolesPipeline = makePipeline(shaderSpan(fillHolesSpirv), "fillHoles", compactionLayout);
    if (isFaulted() || !stepLayout || !compactionLayout) {
        return false;
    }

    nvrhi::CommandListParameters commandParameters;
    commandParameters.queueType = nvrhi::CommandQueue::Compute;
    commands = device->createCommandList(commandParameters);
    timer = device->createTimerQuery();
    stepEventQuery = device->createEventQuery();
    if (!commands || !timer || !stepEventQuery) {
        setFault("failed to create Vulkan command list, timer query or event query");
        return false;
    }
    rebuildBindingSets();
    return !isFaulted();
}

void VulkanBackend::execute(nvrhi::CommandListHandle& commandList) {
    commandList->close();
    device->executeCommandList(commandList, nvrhi::CommandQueue::Compute);
    device->waitForIdle();
}

void VulkanBackend::dispatch(nvrhi::ICommandList* commandList, nvrhi::IComputePipeline* pipeline,
                             nvrhi::IBindingSet* bindings, uint32_t groupsX, uint32_t groupsY) {
    nvrhi::ComputeState state;
    state.pipeline = pipeline;
    state.bindings = {bindings};
    commandList->setComputeState(state);
    commandList->dispatch(groupsX, groupsY, 1);
}

void VulkanBackend::ensureBallCapacity(size_t required) {
    required = std::min(required, capacityCeiling);
    if (isFaulted() || required <= capacity) {
        return;
    }
    const size_t grown = std::max(required, capacity + capacity / capacityGrowthDivisor);
    const size_t newCapacity =
        std::min(((grown + capacityGranule - 1) / capacityGranule) * capacityGranule, capacityCeiling);
    nvrhi::BufferHandle newPositions[2] = {structuredBuffer(newCapacity, true, "positions"),
                                           structuredBuffer(newCapacity, true, "sortedPositions")};
    nvrhi::BufferHandle newStates[2] = {structuredBuffer(newCapacity, true, "states"),
                                        structuredBuffer(newCapacity, true, "sortedStates")};
    nvrhi::BufferHandle newPacked = structuredBuffer(newCapacity, true, "packedBalls");
    if (!newPositions[0] || !newPositions[1] || !newStates[0] || !newStates[1] || !newPacked) {

        if (capacity == 0) {
            setFault(std::format("failed to allocate Vulkan ball buffers for {} balls", newCapacity));
        }
        capacityCeiling = capacity;
        return;
    }

    if (count > 0 && commands) {
        commands->open();
        commands->setBufferState(positions[currentBuffer], nvrhi::ResourceStates::CopySource);
        commands->setBufferState(states[currentBuffer], nvrhi::ResourceStates::CopySource);
        commands->setBufferState(packedBalls, nvrhi::ResourceStates::CopySource);
        commands->setBufferState(newPositions[0], nvrhi::ResourceStates::CopyDest);
        commands->setBufferState(newStates[0], nvrhi::ResourceStates::CopyDest);
        commands->setBufferState(newPacked, nvrhi::ResourceStates::CopyDest);
        commands->commitBarriers();
        const size_t bytes = count * sizeof(uint32_t);
        commands->copyBuffer(newPositions[0], 0, positions[currentBuffer], 0, bytes);
        commands->copyBuffer(newStates[0], 0, states[currentBuffer], 0, bytes);
        commands->copyBuffer(newPacked, 0, packedBalls, 0, bytes);
        execute(commands);
    }

    for (size_t buffer = 0; buffer < 2; ++buffer) {
        positions[buffer] = newPositions[buffer];
        states[buffer] = newStates[buffer];
    }
    packedBalls = newPacked;
    capacity = newCapacity;
    currentBuffer = 0;
    if (stepLayout) {
        rebuildBindingSets();
    }
}

bool VulkanBackend::readBuffer(nvrhi::IBuffer* source, nvrhi::IBuffer* staging, void* destination, size_t bytes) {
    if (bytes == 0) {
        return true;
    }
    commands->open();
    commands->setBufferState(source, nvrhi::ResourceStates::CopySource);
    commands->setBufferState(staging, nvrhi::ResourceStates::CopyDest);
    commands->commitBarriers();
    commands->copyBuffer(staging, 0, source, 0, bytes);
    execute(commands);
    const void* mapped = device->mapBuffer(staging, nvrhi::CpuAccessMode::Read);
    if (!mapped) {
        setFault("failed to map Vulkan readback buffer");
        return false;
    }
    std::memcpy(destination, mapped, bytes);
    device->unmapBuffer(staging);
    return true;
}

bool VulkanBackend::readBallWords(size_t first, size_t words, bool readPositions, bool readStates,
                                  std::vector<uint32_t>& firstWords, std::vector<uint32_t>& secondWords) {
    if (words == 0) {
        firstWords.clear();
        secondWords.clear();
        return true;
    }
    const size_t streams = static_cast<size_t>(readPositions) + static_cast<size_t>(readStates);
    ensureReadback(ballReadback, words * streams * sizeof(uint32_t), "ball");
    if (isFaulted()) {
        return false;
    }
    commands->open();
    size_t destinationOffset = 0;
    if (readPositions) {
        commands->setBufferState(positions[currentBuffer], nvrhi::ResourceStates::CopySource);
        commands->setBufferState(ballReadback, nvrhi::ResourceStates::CopyDest);
        commands->commitBarriers();
        commands->copyBuffer(ballReadback, destinationOffset, positions[currentBuffer], first * sizeof(uint32_t),
                             words * sizeof(uint32_t));
        destinationOffset += words * sizeof(uint32_t);
    }
    if (readStates) {
        commands->setBufferState(states[currentBuffer], nvrhi::ResourceStates::CopySource);
        commands->setBufferState(ballReadback, nvrhi::ResourceStates::CopyDest);
        commands->commitBarriers();
        commands->copyBuffer(ballReadback, destinationOffset, states[currentBuffer], first * sizeof(uint32_t),
                             words * sizeof(uint32_t));
    }
    execute(commands);
    const void* mapped = device->mapBuffer(ballReadback, nvrhi::CpuAccessMode::Read);
    if (!mapped) {
        setFault("failed to map Vulkan ball readback buffer");
        return false;
    }
    const auto* readWords = static_cast<const uint32_t*>(mapped);
    if (readPositions) {
        firstWords.assign(readWords, readWords + words);
    }
    if (readStates) {
        const size_t offset = readPositions ? words : 0;
        secondWords.assign(readWords + offset, readWords + offset + words);
    }
    device->unmapBuffer(ballReadback);
    return true;
}

void VulkanBackend::rebuildBindingSets() {
    if (isFaulted()) {
        return;
    }
    for (uint32_t current = 0; current < 2; ++current) {
        auto makeStepSet = [&](nvrhi::IBuffer* constants) {
            nvrhi::BindingSetDesc description;
            description.bindings = {nvrhi::BindingSetItem::StructuredBuffer_SRV(0, cellWords),
                                    nvrhi::BindingSetItem::StructuredBuffer_SRV(1, targetCells),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(0, positions[current]),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(1, states[current]),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(2, packedBalls),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(3, brickHitsLeft),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(4, killedIndices),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(5, counters),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(6, cellCursors),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(7, positions[1 - current]),
                                    nvrhi::BindingSetItem::StructuredBuffer_UAV(8, states[1 - current]),
                                    nvrhi::BindingSetItem::ConstantBuffer(0, constants)};
            return device->createBindingSet(description, stepLayout);
        };
        stepBindings[current] = makeStepSet(stepConstants);
        countBindings[current] = makeStepSet(countConstants);
        sortBindings[current] = makeStepSet(sortConstants);

        nvrhi::BindingSetDesc compactionDescription;
        compactionDescription.bindings = {nvrhi::BindingSetItem::StructuredBuffer_SRV(0, killedIndices),
                                          nvrhi::BindingSetItem::StructuredBuffer_UAV(0, holes),
                                          nvrhi::BindingSetItem::StructuredBuffer_UAV(1, counters),
                                          nvrhi::BindingSetItem::StructuredBuffer_UAV(2, positions[current]),
                                          nvrhi::BindingSetItem::StructuredBuffer_UAV(3, states[current]),
                                          nvrhi::BindingSetItem::StructuredBuffer_UAV(4, packedBalls),
                                          nvrhi::BindingSetItem::ConstantBuffer(0, compactionConstants)};
        compactionBindings[current] = device->createBindingSet(compactionDescription, compactionLayout);
        if (!stepBindings[current] || !countBindings[current] || !sortBindings[current] || !compactionBindings[current]) {
            setFault("failed to create Vulkan physics binding sets");
            return;
        }
    }
}

void VulkanBackend::uploadBoard(const BoardUpload& board) {
    if (isFaulted()) {
        return;
    }
    fieldWidth = static_cast<float>(board.columns);
    fieldHeight = static_cast<float>(board.rows);
    sortColumns = std::max(board.columns, 0);
    sortRows = std::max(board.rows, 0);

    const bool hitsChanged = !board.brickHitsLeft.empty();
    if (hitsChanged) {
        hostBrickHitsLeft.assign(board.brickHitsLeft.begin(), board.brickHitsLeft.end());
        brickHitsPending = false;
    }
    brickCount = hostBrickHitsLeft.size();
    BallCount possibleKills = 0;
    for (int32_t hits : hostBrickHitsLeft) {
        possibleKills += static_cast<BallCount>(std::max(hits, 0));
    }
    const uint32_t requiredKills =
        static_cast<uint32_t>(std::clamp<BallCount>(possibleKills, minimumKilledCapacity, maximumKillsPerStep));
    const std::vector<int32_t> words = buildCellWords(board);
    std::vector<int32_t> hitsWithSentinel = hostBrickHitsLeft;
    hitsWithSentinel.push_back(0);

    if (!cellWords || cellWords->getDesc().byteSize < std::max<size_t>(words.size(), 1) * sizeof(uint32_t)) {
        cellWords = structuredBuffer(words.size(), false, "cellWords");
    }
    if (!targetCells || targetCells->getDesc().byteSize < std::max<size_t>(board.targetCells.size(), 1) * sizeof(uint32_t)) {
        targetCells = structuredBuffer(board.targetCells.size(), false, "targetCells");
    }
    if (!brickHitsLeft || brickHitsLeft->getDesc().byteSize < hitsWithSentinel.size() * sizeof(uint32_t)) {
        brickHitsLeft = structuredBuffer(hitsWithSentinel.size(), true, "brickHitsLeft");
    }
    if (requiredKills > killedCapacity || !killedIndices || !holes) {
        killedCapacity = std::max(requiredKills, killedCapacity);
        killedIndices = structuredBuffer(killedCapacity, true, "killedIndices");
        holes = structuredBuffer(killedCapacity, true, "holes");
    }
    if (!cellWords || !targetCells || !brickHitsLeft || !killedIndices || !holes) {
        setFault("failed to allocate Vulkan board buffers");
        return;
    }
    rebuildBindingSets();
    if (isFaulted()) {
        return;
    }

    commands->open();
    if (!words.empty()) {
        commands->writeBuffer(cellWords, words.data(), words.size() * sizeof(uint32_t));
    }
    if (!board.targetCells.empty()) {
        commands->writeBuffer(targetCells, board.targetCells.data(), board.targetCells.size_bytes());
    }
    if (hitsChanged) {
        commands->writeBuffer(brickHitsLeft, hitsWithSentinel.data(), hitsWithSentinel.size() * sizeof(uint32_t));
    }
    execute(commands);
    snapshotFresh = false;
}

void VulkanBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty() || isFaulted()) {
        return;
    }
    ensureBallCapacity(count + spawns.size());
    if (isFaulted() || count >= capacity) {
        return;
    }
    spawns = spawns.first(std::min(spawns.size(), capacity - count));
    std::vector<uint32_t> positionsToAdd(spawns.size());
    std::vector<uint32_t> statesToAdd(spawns.size());
    std::vector<uint32_t> packedToAdd(spawns.size());
    ballSpeed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeed = 1.0f / ballSpeed;
    for (size_t index = 0; index < spawns.size(); ++index) {
        const BallSpawn& spawn = spawns[index];
        const uint32_t color = static_cast<uint32_t>(spawn.color);
        positionsToAdd[index] = encodeAxis(spawn.x, fieldWidth) | (encodeAxis(spawn.y, fieldHeight) << 16);
        statesToAdd[index] = encodeComponent(spawn.vx * inverseSpeed) |
                             (encodeComponent(spawn.vy * inverseSpeed) << directionBits) | (color << colorShift);
        packedToAdd[index] = packBallForRender(spawn.x, spawn.y, spawn.color, renderPackScale(fieldWidth),
                                               renderPackScale(fieldHeight));
    }
    const size_t byteOffset = count * sizeof(uint32_t);
    commands->open();
    commands->writeBuffer(positions[currentBuffer], positionsToAdd.data(), positionsToAdd.size() * sizeof(uint32_t),
                          byteOffset);
    commands->writeBuffer(states[currentBuffer], statesToAdd.data(), statesToAdd.size() * sizeof(uint32_t), byteOffset);
    commands->writeBuffer(packedBalls, packedToAdd.data(), packedToAdd.size() * sizeof(uint32_t), byteOffset);
    execute(commands);
    count += spawns.size();
    sortSchedule.ballsAdded(spawns.size(), count);
    snapshotFresh = false;
}

void VulkanBackend::beginStep(const StepParams& params) {
    snapshotFresh = false;
    lastCounters.fill(0);
    lastStepKernelMs = 0.0;
    if (count == 0 || isFaulted()) {
        return;
    }
    if (count > std::numeric_limits<uint32_t>::max()) {
        setFault("Vulkan ball count exceeds the shader's 32-bit index range");
        return;
    }
    ballSpeed = std::max(params.speed, 1e-6f);
    const size_t stepItemsPerGroup = static_cast<size_t>(tuning.threadsPerGroup) * tuning.ballsPerThread;
    const size_t countItemsPerGroup = static_cast<size_t>(tuning.countThreads) * tuning.countItems;
    const size_t sortItemsPerGroup = static_cast<size_t>(tuning.sortThreads) * tuning.sortItems;
    const DispatchShape stepShape = shapeFor(count, stepItemsPerGroup);
    const DispatchShape countShape = shapeFor(count, countItemsPerGroup);
    const DispatchShape sortShape = shapeFor(count, sortItemsPerGroup);
    const size_t sortCellCount = static_cast<size_t>(std::max(sortColumns, 0)) * static_cast<size_t>(std::max(sortRows, 0));
    const bool sorting = sortCellCount > 0 && sortCellCount <= maximumSortCells && sortSchedule.sortThisTick(count);
    const uint32_t ballCount = static_cast<uint32_t>(count);
    StepConstants stepData{shaderStepParams(params), ballCount, killedCapacity, stepShape.groupsPerRow, static_cast<uint32_t>(capacity),
                           0u, sortColumns, sortRows, 0u, 0u};
    StepConstants countData = stepData;
    countData.groupsPerRow = countShape.groupsPerRow;
    countData.toSorted = 1u;
    StepConstants sortData = stepData;
    sortData.groupsPerRow = sortShape.groupsPerRow;
    sortData.toSorted = 1u;
    const std::array<uint32_t, counterWordCount> zeroCounters{};
    const std::array<uint32_t, maximumSortCells> zeroCursors{};

    commands->open();
    if (brickHitsPending && brickCount > 0) {
        commands->writeBuffer(brickHitsLeft, hostBrickHitsLeft.data(), brickCount * sizeof(uint32_t));
    }
    brickHitsPending = false;
    commands->writeBuffer(counters, zeroCounters.data(), sizeof(zeroCounters));
    commands->writeBuffer(stepConstants, &stepData, sizeof(stepData));
    commands->writeBuffer(countConstants, &countData, sizeof(countData));
    commands->writeBuffer(sortConstants, &sortData, sizeof(sortData));
    if (sorting) {
        commands->writeBuffer(cellCursors, zeroCursors.data(), sizeof(zeroCursors));
    }
    commands->beginTimerQuery(timer);
    if (sorting) {
        dispatch(commands, countPipeline, countBindings[currentBuffer], countShape.groupsPerRow, countShape.rows);
        commands->setBufferState(cellCursors, nvrhi::ResourceStates::UnorderedAccess);
        commands->commitBarriers();
        dispatch(commands, scanPipeline, countBindings[currentBuffer], 1, 1);
        commands->setBufferState(cellCursors, nvrhi::ResourceStates::UnorderedAccess);
        commands->commitBarriers();
        dispatch(commands, sortPipeline, sortBindings[currentBuffer], sortShape.groupsPerRow, sortShape.rows);
    } else {
        dispatch(commands, stepPipeline, stepBindings[currentBuffer], stepShape.groupsPerRow, stepShape.rows);
    }
    commands->endTimerQuery(timer);
    commands->setBufferState(counters, nvrhi::ResourceStates::CopySource);
    commands->setBufferState(countersReadback, nvrhi::ResourceStates::CopyDest);
    commands->commitBarriers();
    commands->copyBuffer(countersReadback, 0, counters, 0, sizeof(zeroCounters));

    commands->close();
    device->executeCommandList(commands, nvrhi::CommandQueue::Compute);
    if (stepEventQuery) {
        device->resetEventQuery(stepEventQuery);
        device->setEventQuery(stepEventQuery, nvrhi::CommandQueue::Compute);
    }
    if (sorting) {
        currentBuffer = 1u - currentBuffer;
    }
}

void VulkanBackend::finishStep(std::vector<BallHit>& hits) {
    static_cast<void>(hits);
    if (count == 0 || isFaulted()) {
        return;
    }
    if (stepEventQuery) {
        device->waitEventQuery(stepEventQuery);
    }
    const double measuredKernelMs = static_cast<double>(device->getTimerQueryTime(timer)) * 1000.0;
    device->resetTimerQuery(timer);
    lastStepKernelMs = 0.0;
    if (std::isfinite(measuredKernelMs) && measuredKernelMs >= 0.0 && measuredKernelMs <= 1000.0) {
        lastStepKernelMs = measuredKernelMs;
        sortSchedule.record(lastStepKernelMs, count);
    }

    const void* mappedCounters = device->mapBuffer(countersReadback, nvrhi::CpuAccessMode::Read);
    if (!mappedCounters) {
        setFault("failed to map Vulkan frame counters");
        return;
    }
    std::memcpy(lastCounters.data(), mappedCounters, sizeof(lastCounters));
    device->unmapBuffer(countersReadback);
    lastCounters[0] = std::min(lastCounters[0], killedCapacity);
    if (lastCounters[0] == 0) {
        return;
    }
    compactKilledBalls(lastCounters[0]);
}

void VulkanBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    beginStep(params);
    finishStep(hits);
}

void VulkanBackend::compactKilledBalls(uint32_t killed) {
    if (killed == 0 || killed > count || isFaulted()) {
        return;
    }
    const uint32_t oldCount = static_cast<uint32_t>(count);
    const uint32_t survivorCount = oldCount - killed;
    const CompactionConstants constants{killed, survivorCount, oldCount, static_cast<uint32_t>(capacity)};
    const DispatchShape shape = shapeFor(killed, 256);
    commands->open();
    commands->writeBuffer(compactionConstants, &constants, sizeof(constants));
    commands->setBufferState(killedIndices, nvrhi::ResourceStates::ShaderResource);
    commands->setBufferState(holes, nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(counters, nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(positions[currentBuffer], nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(states[currentBuffer], nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(packedBalls, nvrhi::ResourceStates::UnorderedAccess);
    commands->commitBarriers();
    dispatch(commands, collectHolesPipeline, compactionBindings[currentBuffer], shape.groupsPerRow, shape.rows);
    commands->setBufferState(holes, nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(counters, nvrhi::ResourceStates::UnorderedAccess);
    commands->commitBarriers();
    dispatch(commands, fillHolesPipeline, compactionBindings[currentBuffer], shape.groupsPerRow, shape.rows);

    if (brickCount > 0) {
        ensureReadback(hitsReadback, brickCount * sizeof(uint32_t), "brick hit");
        commands->setBufferState(brickHitsLeft, nvrhi::ResourceStates::CopySource);
        commands->setBufferState(hitsReadback, nvrhi::ResourceStates::CopyDest);
        commands->commitBarriers();
        commands->copyBuffer(hitsReadback, 0, brickHitsLeft, 0, brickCount * sizeof(uint32_t));
    }
    execute(commands);
    count = survivorCount;
    snapshotFresh = false;
    if (brickCount > 0) {
        const void* mappedHits = device->mapBuffer(hitsReadback, nvrhi::CpuAccessMode::Read);
        if (!mappedHits) {
            setFault("failed to map Vulkan brick hit readback buffer");
            return;
        }
        std::memcpy(hostBrickHitsLeft.data(), mappedHits, brickCount * sizeof(uint32_t));
        device->unmapBuffer(hitsReadback);
    }
}

void VulkanBackend::uploadBrickHits(std::span<const int32_t> hitsLeft) {
    if (isFaulted() || !brickHitsLeft || hitsLeft.size() != brickCount) {
        return;
    }
    hostBrickHitsLeft.assign(hitsLeft.begin(), hitsLeft.end());
    brickHitsPending = true;
}

DeviceHitResults VulkanBackend::deviceHitResults() const {
    DeviceHitResults results;
    results.killed = lastCounters[0];
    if (results.killed == 0) {
        return results;
    }
    results.brickHitsLeft = std::span<const int32_t>(hostBrickHitsLeft.data(), brickCount);
    for (int color = 0; color < ballColorCount; ++color) {
        results.killsByColor[static_cast<size_t>(color)] = lastCounters[3 + static_cast<size_t>(color)];
    }
    return results;
}

BallSnapshot VulkanBackend::snapshot() {
    if (isFaulted()) {
        return {};
    }
    if (!snapshotFresh) {
        ensureReadback(packedReadback, count * sizeof(uint32_t), "packed ball");
        if (isFaulted()) {
            return {};
        }
        hostPacked.resize(count);
        if (count > 0 && !readBuffer(packedBalls, packedReadback, hostPacked.data(), count * sizeof(uint32_t))) {
            return {};
        }
        snapshotFresh = true;
    }
    return {hostPacked.data(), hostPacked.size(), fieldWidth, fieldHeight, false, false};
}

std::vector<BallSpawn> VulkanBackend::exportRange(size_t first) {
    if (isFaulted() || first >= count) {
        return {};
    }
    std::vector<uint32_t> positionsFromDevice;
    std::vector<uint32_t> statesFromDevice;
    const size_t rangeCount = count - first;
    if (!readBallWords(first, rangeCount, true, true, positionsFromDevice, statesFromDevice)) {
        return {};
    }
    std::vector<BallSpawn> exported;
    exported.reserve(rangeCount);
    for (size_t index = 0; index < rangeCount; ++index) {
        const uint32_t state = statesFromDevice[index];
        const uint32_t color = stateColor(state);
        if (color == deadColor) {
            continue;
        }
        const float dx = static_cast<float>(signedComponent(state & directionMask));
        const float dy = static_cast<float>(signedComponent((state >> directionBits) & directionMask));
        const float length = std::max(std::hypot(dx, dy), 1.0f);
        const uint32_t position = positionsFromDevice[index];
        exported.push_back({static_cast<float>(position & 0xFFFFu) * fieldWidth / positionScale,
                            static_cast<float>(position >> 16) * fieldHeight / positionScale, dx / length * ballSpeed,
                            dy / length * ballSpeed, static_cast<int32_t>(color)});
    }
    return exported;
}

std::vector<BallSpawn> VulkanBackend::exportBalls() { return exportRange(0); }

std::vector<BallSpawn> VulkanBackend::takeTail(size_t tailCount) {
    const size_t first = count - std::min(tailCount, count);
    std::vector<BallSpawn> tail = exportRange(first);
    if (!isFaulted()) {
        count = first;
        snapshotFresh = false;
    }
    return tail;
}

bool VulkanBackend::takeCompactTail(size_t tailCount, CompactBalls& taken) {
    if (isFaulted()) {
        return false;
    }
    const size_t first = count - std::min(tailCount, count);
    const size_t takenCount = count - first;
    return readBallWords(first, takenCount, true, true, taken.position, taken.state) && [&] {
        count = first;
        snapshotFresh = false;
        return true;
    }();
}

bool VulkanBackend::addCompact(const CompactBalls& added) {
    if (isFaulted()) {
        return false;
    }
    const size_t addedCount = std::min(added.position.size(), added.state.size());
    if (addedCount == 0) {
        return true;
    }
    ensureBallCapacity(count + addedCount);
    if (isFaulted()) {
        return false;
    }
    std::vector<uint32_t> packed(addedCount);
    for (size_t index = 0; index < addedCount; ++index) {
        packed[index] = packCompactForRender(added.position[index], added.state[index]);
    }
    const size_t byteOffset = count * sizeof(uint32_t);
    commands->open();
    commands->writeBuffer(positions[currentBuffer], added.position.data(), addedCount * sizeof(uint32_t), byteOffset);
    commands->writeBuffer(states[currentBuffer], added.state.data(), addedCount * sizeof(uint32_t), byteOffset);
    commands->writeBuffer(packedBalls, packed.data(), addedCount * sizeof(uint32_t), byteOffset);
    execute(commands);
    count += addedCount;
    sortSchedule.ballsAdded(addedCount, count);
    snapshotFresh = false;
    return !isFaulted();
}

BallTally VulkanBackend::colorTally(size_t first, size_t last) {
    BallTally tally{};
    if (isFaulted() || first >= last) {
        return tally;
    }
    std::vector<uint32_t> statesFromDevice;
    std::vector<uint32_t> unused;
    if (!readBallWords(first, last - first, false, true, unused, statesFromDevice)) {
        return tally;
    }
    for (uint32_t state : statesFromDevice) {
        const uint32_t color = stateColor(state);
        if (color != deadColor && color < ballColorCount) {
            ++tally[color];
        }
    }
    return tally;
}

BallTally VulkanBackend::truncate(size_t newCount) {
    if (isFaulted() || newCount >= count) {
        return {};
    }
    const BallTally removed = colorTally(newCount, count);
    if (!isFaulted()) {
        count = newCount;
        snapshotFresh = false;
    }
    return removed;
}

void VulkanBackend::clear() {
    count = 0;
    snapshotFresh = false;
}

