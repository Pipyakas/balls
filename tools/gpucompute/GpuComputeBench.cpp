#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"
#include "sim/SortSchedule.h"
#include "sim/TargetField.h"

#include "generated/BallStepBlobs.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <nvrhi/d3d11.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

constexpr uint32_t maximumGroupsPerRow = 32768;
constexpr int emptyFarCell = -1;
constexpr int emptyNearCell = -2;
constexpr int cellBrickShift = 3;
constexpr int counterWords = 10;
constexpr int automaticSortInterval = -1;
constexpr int warmupTicks = 10;
constexpr float positionScale = 65535.0f;
constexpr int directionBits = 13;
constexpr uint32_t directionMask = (1u << directionBits) - 1u;
constexpr float directionScale = 4095.0f;
constexpr uint32_t colorShift = 26u;
constexpr uint32_t minimumKilledCapacity = 1u << 16;
constexpr size_t maximumSortCells = 4096;

struct StepConstants {
    StepParams params;
    uint32_t ballCount;
    uint32_t killedCapacity;
    uint32_t groupsPerRow;
    uint32_t packedCapacity;
    uint32_t toSorted;
    int32_t sortColumns;
    int32_t sortRows;
    uint32_t padding0;
    uint32_t padding1;
};

struct Options {
    std::string levelDirectory = "assets/levels/performance";
    std::string levelName = "DA_1000Bricks";
    std::vector<std::string> apis{"d3d11", "d3d12", "vulkan"};
    size_t balls = 16'000'000;
    int ticks = 200;
    int substeps = 2;
    int sortInterval = automaticSortInterval;
    uint32_t ballsPerStepGroup = 1024;
    uint32_t ballsPerCountGroup = 4096;
    uint32_t ballsPerSortGroup = 4096;
    std::string blobDirectory;
};

struct Workload {
    StepParams params{};
    std::vector<int32_t> cellWords;
    std::vector<int32_t> targetCells;
    std::vector<int32_t> brickHitsLeft;
    std::vector<uint32_t> positions;
    std::vector<uint32_t> states;
    uint32_t killedCapacity = minimumKilledCapacity;
};

class StderrMessages final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity >= nvrhi::MessageSeverity::Warning) {
            std::fprintf(stderr, "nvrhi: %s\n", text);
        }
    }
};

StderrMessages messages;

struct ShaderSet {
    std::vector<uint8_t> step;
    std::vector<uint8_t> count;
    std::vector<uint8_t> scan;
    std::vector<uint8_t> sort;
};

template <size_t StepSize, size_t CountSize, size_t ScanSize, size_t SortSize>
ShaderSet shaderSet(const unsigned char (&step)[StepSize], const unsigned char (&count)[CountSize],
                    const unsigned char (&scan)[ScanSize], const unsigned char (&sort)[SortSize]) {
    return {{std::begin(step), std::end(step)},
            {std::begin(count), std::end(count)},
            {std::begin(scan), std::end(scan)},
            {std::begin(sort), std::end(sort)}};
}

struct Device {
    std::string name;
    nvrhi::DeviceHandle device;
    ShaderSet shaders;
    std::function<void()> release;
};

ComPtr<IDXGIAdapter1> highPerformanceAdapter(std::string& name) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        return nullptr;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;
         factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) !=
         DXGI_ERROR_NOT_FOUND;
         ++index) {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            char narrow[256]{};
            WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, narrow, sizeof(narrow), nullptr, nullptr);
            name = narrow;
            return adapter;
        }
    }
    return nullptr;
}

std::unique_ptr<Device> createD3d12() {
    auto result = std::make_unique<Device>();
    std::string adapterName;
    ComPtr<IDXGIAdapter1> adapter = highPerformanceAdapter(adapterName);
    ComPtr<ID3D12Device> device;
    if (!adapter || FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
        return nullptr;
    }
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))) {
        return nullptr;
    }
    nvrhi::d3d12::DeviceDesc description;
    description.errorCB = &messages;
    description.pDevice = device.Get();
    description.pGraphicsCommandQueue = queue.Get();
    result->device = nvrhi::d3d12::createDevice(description);
    result->name = "D3D12 " + adapterName;
    result->shaders = shaderSet(ballStepDxil, countCellsDxil, scanCellsDxil, stepAndSortDxil);
    result->release = [device, queue] {};
    return result->device ? std::move(result) : nullptr;
}

std::unique_ptr<Device> createD3d11() {
    auto result = std::make_unique<Device>();
    std::string adapterName;
    ComPtr<IDXGIAdapter1> adapter = highPerformanceAdapter(adapterName);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    if (!adapter || FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
                                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr,
                                             &context))) {
        return nullptr;
    }
    nvrhi::d3d11::DeviceDesc description;
    description.messageCallback = &messages;
    description.context = context.Get();
    result->device = nvrhi::d3d11::createDevice(description);
    result->name = "D3D11 " + adapterName;
    result->shaders = shaderSet(ballStepDxbc, countCellsDxbc, scanCellsDxbc, stepAndSortDxbc);
    result->release = [device, context] {};
    return result->device ? std::move(result) : nullptr;
}

auto& vk() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

std::unique_ptr<Device> createVulkan() {
    HMODULE loader = LoadLibraryA("vulkan-1.dll");
    const auto getInstanceProcAddr =
        loader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr")) : nullptr;
    if (!getInstanceProcAddr) {
        return nullptr;
    }
    vk().init(getInstanceProcAddr);
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "balls_gpucompute";
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (vk().vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        return nullptr;
    }
    vk().init(vk::Instance(instance));
    uint32_t count = 0;
    vk().vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> candidates(count);
    vk().vkEnumeratePhysicalDevices(instance, &count, candidates.data());
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    std::string adapterName;
    for (VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties properties{};
        vk().vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        uint32_t familyCount = 0;
        vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[family].queueFlags & needed) == needed) {
                physicalDevice = candidate;
                queueFamily = family;
                adapterName = properties.deviceName;
                break;
            }
        }
        if (physicalDevice) {
            break;
        }
    }
    if (!physicalDevice) {
        return nullptr;
    }
    VkPhysicalDeviceVulkan13Features vulkan13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    vulkan13.synchronization2 = VK_TRUE;
    vulkan13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    vulkan12.timelineSemaphore = VK_TRUE;
    vulkan12.pNext = &vulkan13;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &vulkan12;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    if (vk().vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &logicalDevice) != VK_SUCCESS) {
        return nullptr;
    }
    vk().init(vk::Device(logicalDevice));
    VkQueue queue = VK_NULL_HANDLE;
    vk().vkGetDeviceQueue(logicalDevice, queueFamily, 0, &queue);
    nvrhi::vulkan::DeviceDesc description;
    description.errorCB = &messages;
    description.instance = instance;
    description.physicalDevice = physicalDevice;
    description.device = logicalDevice;
    description.graphicsQueue = queue;
    description.graphicsQueueIndex = static_cast<int>(queueFamily);
    auto result = std::make_unique<Device>();
    result->device = nvrhi::vulkan::createDevice(description);
    result->name = "Vulkan " + adapterName;
    result->shaders = shaderSet(ballStepSpirv, countCellsSpirv, scanCellsSpirv, stepAndSortSpirv);
    result->release = [instance, logicalDevice] {
        vk().vkDeviceWaitIdle(logicalDevice);
        vk().vkDestroyDevice(logicalDevice, nullptr);
        vk().vkDestroyInstance(instance, nullptr);
    };
    return result->device ? std::move(result) : nullptr;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void loadBlobOverrides(Device& target, const std::string& api, const std::string& directory) {
    if (directory.empty()) {
        return;
    }
    const std::string suffix = api == "d3d11" ? ".dxbc" : (api == "d3d12" ? ".dxil" : ".spv");
    const std::filesystem::path root(directory);
    target.shaders = {readFile(root / ("ballStep" + suffix)), readFile(root / ("countCells" + suffix)),
                      readFile(root / ("scanCells" + suffix)), readFile(root / ("stepAndSort" + suffix))};
}

std::unique_ptr<Device> createDevice(const std::string& api) {
    if (api == "d3d11") {
        return createD3d11();
    }
    if (api == "d3d12") {
        return createD3d12();
    }
    if (api == "vulkan") {
        return createVulkan();
    }
    return nullptr;
}

uint32_t encodeAxis(float value, float extent) {
    const float scaled = value / extent * positionScale + 0.5f;
    return static_cast<uint32_t>(std::clamp(scaled, 0.0f, positionScale));
}

uint32_t encodeComponent(float value) {
    const int quantized = static_cast<int>(std::lrint(std::clamp(value, -1.0f, 1.0f) * directionScale));
    return static_cast<uint32_t>(quantized) & directionMask;
}

BrickLayout layoutFor(const Options& options, bool& ok) {
    BrickLayout layout;
    ok = true;
    if (options.levelName == "empty") {
        constexpr int emptyWidth = 30;
        constexpr int emptyRows = 38;
        layout.width = emptyWidth;
        layout.height = 1;
        layout.playableHeight = emptyRows;
        layout.cellIds.assign(emptyWidth, emptyBrickId);
        return layout;
    }
    auto level = loadLevel(levelFilesFor(options.levelDirectory, options.levelName));
    if (!level) {
        std::fprintf(stderr, "level: %s\n", level.error().c_str());
        ok = false;
        return layout;
    }
    return level->bricks;
}

Workload buildWorkload(const BrickLayout& layout, const Options& options) {
    Workload work;
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(layout);
    const BrickBoard& board = simulation.board();
    work.params = simulation.stepParams(options.substeps);
    TargetField field;
    field.rebuild(board);
    const auto paddedCells = board.paddedCells();
    const auto brickColors = board.brickColors();
    const std::vector<int32_t>& nearBrick = board.nearBrickFlags();
    work.cellWords.assign(paddedCells.size(), emptyFarCell);
    for (size_t cell = 0; cell < paddedCells.size(); ++cell) {
        const int32_t brick = paddedCells[cell];
        if (brick >= 0) {
            work.cellWords[cell] = (brick << cellBrickShift) | brickColors[static_cast<size_t>(brick)];
        } else if (nearBrick[cell] != 0) {
            work.cellWords[cell] = emptyNearCell;
        }
    }
    work.targetCells.assign(field.targetCells().begin(), field.targetCells().end());
    work.brickHitsLeft = board.hitsLeftPerBrick();
    work.brickHitsLeft.push_back(0);
    uint32_t totalHits = 0;
    for (int32_t hits : work.brickHitsLeft) {
        totalHits += static_cast<uint32_t>(std::max(hits, 0));
    }
    work.killedCapacity = std::max(totalHits, minimumKilledCapacity);

    std::mt19937 random(1234u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDistribution(0, ballColorCount - 1);
    work.positions.reserve(options.balls);
    work.states.reserve(options.balls);
    const float width = static_cast<float>(board.columns());
    const float height = static_cast<float>(board.rows());
    while (work.positions.size() < options.balls) {
        const float x = unit(random) * width;
        const float y = unit(random) * height;
        if (board.brickAt(static_cast<int>(x), static_cast<int>(y)) >= 0) {
            continue;
        }
        const float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
        const uint32_t color = static_cast<uint32_t>(colorDistribution(random));
        work.positions.push_back(encodeAxis(x, width) | (encodeAxis(y, height) << 16));
        work.states.push_back(encodeComponent(std::cos(angle)) | (encodeComponent(std::sin(angle)) << directionBits) |
                              (color << colorShift));
    }
    return work;
}

nvrhi::BufferHandle structuredBuffer(nvrhi::IDevice* device, size_t elements, bool writable, const char* name) {
    nvrhi::BufferDesc description;
    description.byteSize = std::max<size_t>(elements, 1) * 4;
    description.structStride = 4;
    description.canHaveUAVs = writable;
    description.initialState = writable ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;
    description.keepInitialState = true;
    description.debugName = name;
    return device->createBuffer(description);
}

std::vector<uint32_t> readBack(nvrhi::IDevice* device, nvrhi::ICommandList* commands, nvrhi::IBuffer* source,
                               size_t words) {
    nvrhi::BufferDesc description;
    description.byteSize = words * 4;
    description.cpuAccess = nvrhi::CpuAccessMode::Read;
    description.initialState = nvrhi::ResourceStates::CopyDest;
    description.keepInitialState = true;
    description.debugName = "readback";
    nvrhi::BufferHandle staging = device->createBuffer(description);
    commands->open();
    commands->copyBuffer(staging, 0, source, 0, words * 4);
    commands->close();
    device->executeCommandList(commands);
    device->waitForIdle();
    std::vector<uint32_t> values(words);
    if (const void* mapped = device->mapBuffer(staging, nvrhi::CpuAccessMode::Read)) {
        std::memcpy(values.data(), mapped, words * 4);
        device->unmapBuffer(staging);
    }
    return values;
}

std::array<size_t, 8> colorTally(const std::vector<uint32_t>& states) {
    std::array<size_t, 8> tally{};
    for (uint32_t state : states) {
        ++tally[(state >> colorShift) & 7u];
    }
    return tally;
}

struct DispatchShape {
    uint32_t groupsPerRow = 1;
    uint32_t rows = 1;
};

DispatchShape shapeFor(size_t items, uint32_t itemsPerGroup) {
    const uint32_t groups = static_cast<uint32_t>((items + itemsPerGroup - 1) / itemsPerGroup);
    const uint32_t perRow = std::max(std::min(groups, maximumGroupsPerRow), 1u);
    return {perRow, (groups + perRow - 1) / perRow};
}

nvrhi::ComputePipelineHandle computePipeline(nvrhi::IDevice* device, const std::vector<uint8_t>& blob,
                                             nvrhi::IBindingLayout* layout) {
    nvrhi::ShaderDesc shaderDescription;
    shaderDescription.shaderType = nvrhi::ShaderType::Compute;
    shaderDescription.entryName = "computeMain";
    nvrhi::ShaderHandle shader = device->createShader(shaderDescription, blob.data(), blob.size());
    nvrhi::ComputePipelineDesc description;
    description.CS = shader;
    description.bindingLayouts = {layout};
    return shader ? device->createComputePipeline(description) : nullptr;
}

nvrhi::BufferHandle constantBuffer(nvrhi::IDevice* device, const char* name) {
    nvrhi::BufferDesc description;
    description.byteSize = sizeof(StepConstants);
    description.isConstantBuffer = true;
    description.initialState = nvrhi::ResourceStates::ConstantBuffer;
    description.keepInitialState = true;
    description.debugName = name;
    return device->createBuffer(description);
}

void runBench(Device& target, const Workload& work, const Options& options) {
    nvrhi::IDevice* device = target.device;
    const size_t count = work.positions.size();
    const bool sorting = options.sortInterval != 0;
    const bool automaticSorting = options.sortInterval == automaticSortInterval;
    SortSchedule schedule;
    nvrhi::BufferHandle cellWords = structuredBuffer(device, work.cellWords.size(), false, "cellWords");
    nvrhi::BufferHandle targetCells = structuredBuffer(device, work.targetCells.size(), false, "targetCells");
    nvrhi::BufferHandle positions[2] = {structuredBuffer(device, count, true, "positions"),
                                        structuredBuffer(device, sorting ? count : 1, true, "sortedPositions")};
    nvrhi::BufferHandle states[2] = {structuredBuffer(device, count, true, "states"),
                                     structuredBuffer(device, sorting ? count : 1, true, "sortedStates")};
    nvrhi::BufferHandle packed = structuredBuffer(device, count, true, "packed");
    nvrhi::BufferHandle hitsLeft = structuredBuffer(device, work.brickHitsLeft.size(), true, "brickHitsLeft");
    nvrhi::BufferHandle killed = structuredBuffer(device, work.killedCapacity, true, "killedIndices");
    nvrhi::BufferHandle counters = structuredBuffer(device, counterWords, true, "counters");
    nvrhi::BufferHandle cellCursors = structuredBuffer(device, maximumSortCells, true, "cellCursors");
    nvrhi::BufferHandle stepConstantBuffer = constantBuffer(device, "stepConstants");
    nvrhi::BufferHandle sortConstantBuffer = constantBuffer(device, "sortConstants");
    nvrhi::BufferHandle countConstantBuffer = constantBuffer(device, "countConstants");

    const DispatchShape stepShape = shapeFor(count, options.ballsPerStepGroup);
    const DispatchShape countShape = shapeFor(count, options.ballsPerCountGroup);
    const DispatchShape sortShape = shapeFor(count, options.ballsPerSortGroup);
    const int sortColumns = static_cast<int>(work.params.fieldWidth);
    const int sortRows = static_cast<int>(work.params.fieldHeight);
    const StepConstants stepConstants{work.params,   static_cast<uint32_t>(count), work.killedCapacity,
                                      stepShape.groupsPerRow, static_cast<uint32_t>(count), 0u, sortColumns, sortRows,
                                      0u,            0u};
    StepConstants sortConstants = stepConstants;
    sortConstants.groupsPerRow = sortShape.groupsPerRow;
    sortConstants.toSorted = 1u;
    StepConstants countConstants = sortConstants;
    countConstants.groupsPerRow = countShape.groupsPerRow;

    nvrhi::CommandListHandle commands = device->createCommandList();
    commands->open();
    commands->writeBuffer(cellWords, work.cellWords.data(), work.cellWords.size() * 4);
    commands->writeBuffer(targetCells, work.targetCells.data(), work.targetCells.size() * 4);
    commands->writeBuffer(positions[0], work.positions.data(), count * 4);
    commands->writeBuffer(states[0], work.states.data(), count * 4);
    commands->writeBuffer(hitsLeft, work.brickHitsLeft.data(), work.brickHitsLeft.size() * 4);
    commands->writeBuffer(stepConstantBuffer, &stepConstants, sizeof(stepConstants));
    commands->writeBuffer(sortConstantBuffer, &sortConstants, sizeof(sortConstants));
    commands->writeBuffer(countConstantBuffer, &countConstants, sizeof(countConstants));
    commands->close();
    device->executeCommandList(commands);
    device->waitForIdle();

    nvrhi::BindingLayoutDesc layoutDescription;
    layoutDescription.visibility = nvrhi::ShaderType::Compute;
    layoutDescription.bindings = {nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
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
    nvrhi::BindingLayoutHandle layout = device->createBindingLayout(layoutDescription);
    auto bindingSet = [&](int current, nvrhi::IBuffer* constants) {
        nvrhi::BindingSetDesc description;
        description.bindings = {nvrhi::BindingSetItem::StructuredBuffer_SRV(0, cellWords),
                                nvrhi::BindingSetItem::StructuredBuffer_SRV(1, targetCells),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, positions[current]),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(1, states[current]),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(2, packed),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(3, hitsLeft),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(4, killed),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(5, counters),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(6, cellCursors),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(7, positions[1 - current]),
                                nvrhi::BindingSetItem::StructuredBuffer_UAV(8, states[1 - current]),
                                nvrhi::BindingSetItem::ConstantBuffer(0, constants)};
        return device->createBindingSet(description, layout);
    };
    const nvrhi::BindingSetHandle stepBindings[2] = {bindingSet(0, stepConstantBuffer), bindingSet(1, stepConstantBuffer)};
    const nvrhi::BindingSetHandle sortBindings[2] = {bindingSet(0, sortConstantBuffer), bindingSet(1, sortConstantBuffer)};
    const nvrhi::BindingSetHandle countBindings[2] = {bindingSet(0, countConstantBuffer),
                                                      bindingSet(1, countConstantBuffer)};
    const nvrhi::ComputePipelineHandle stepPipeline = computePipeline(device, target.shaders.step, layout);
    const nvrhi::ComputePipelineHandle countPipeline = computePipeline(device, target.shaders.count, layout);
    const nvrhi::ComputePipelineHandle scanPipeline = computePipeline(device, target.shaders.scan, layout);
    const nvrhi::ComputePipelineHandle sortPipeline = computePipeline(device, target.shaders.sort, layout);
    if (!stepPipeline || !countPipeline || !scanPipeline || !sortPipeline || !stepBindings[0] || !sortBindings[0]) {
        std::printf("%-34s pipeline creation failed\n", target.name.c_str());
        return;
    }
    nvrhi::TimerQueryHandle timer = device->createTimerQuery();
    const uint32_t zeroCounters[counterWords]{};
    const std::vector<uint32_t> zeroCursors(maximumSortCells, 0u);
    auto dispatch = [&](nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* bindings, DispatchShape shape) {
        nvrhi::ComputeState state;
        state.pipeline = pipeline;
        state.bindings = {bindings};
        commands->setComputeState(state);
        commands->dispatch(shape.groupsPerRow, shape.rows, 1);
    };

    auto waitForCursors = [&] {
        commands->setBufferState(cellCursors, nvrhi::ResourceStates::UnorderedAccess);
        commands->commitBarriers();
    };

    int current = 0;
    double gpuSeconds = 0.0;
    double wallSeconds = 0.0;
    for (int tick = 0; tick < warmupTicks + options.ticks; ++tick) {
        const bool sortThisTick = automaticSorting ? schedule.sortThisTick(count)
                                                   : sorting && tick % options.sortInterval == 0;
        const Clock::time_point start = Clock::now();
        commands->open();
        commands->writeBuffer(counters, zeroCounters, sizeof(zeroCounters));
        if (sortThisTick) {
            commands->writeBuffer(cellCursors, zeroCursors.data(), zeroCursors.size() * 4);
        }
        commands->beginTimerQuery(timer);
        if (sortThisTick) {
            dispatch(countPipeline, countBindings[current], countShape);
            waitForCursors();
            dispatch(scanPipeline, countBindings[current], DispatchShape{});
            waitForCursors();
            dispatch(sortPipeline, sortBindings[current], sortShape);
        } else {
            dispatch(stepPipeline, stepBindings[current], stepShape);
        }
        commands->endTimerQuery(timer);
        commands->close();
        device->executeCommandList(commands);
        device->waitForIdle();
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        const double kernel = device->getTimerQueryTime(timer);
        device->resetTimerQuery(timer);
        if (automaticSorting) {
            schedule.record(kernel * 1000.0, count);
        }
        if (sortThisTick) {
            current = 1 - current;
        }
        if (tick >= warmupTicks) {
            gpuSeconds += kernel;
            wallSeconds += elapsed;
        }
    }
    const double kernelMs = gpuSeconds * 1000.0 / options.ticks;
    const double wallMs = wallSeconds * 1000.0 / options.ticks;
    const std::array<size_t, 8> before = colorTally(work.states);
    const std::array<size_t, 8> after = colorTally(readBack(device, commands, states[current], count));
    const std::vector<uint32_t> finalCounters = readBack(device, commands, counters, counterWords);
    bool conserved = after[7] <= work.killedCapacity;
    for (size_t color = 0; color < 7; ++color) {
        conserved = conserved && after[color] <= before[color];
    }
    size_t lost = 0;
    for (size_t color = 0; color < 7; ++color) {
        lost += before[color] - std::min(before[color], after[color]);
    }
    conserved = conserved && lost == after[7];
    std::printf("%-34s balls=%9zu  kernel %8.3f ms  wall %8.3f ms  %8.1f M ball-steps/s  dead %zu %s  sort every %d\n",
                target.name.c_str(), count, kernelMs, wallMs, static_cast<double>(count) / (kernelMs / 1000.0) / 1e6,
                after[7], conserved ? "conserved" : "MISMATCH",
                automaticSorting ? schedule.interval() : std::max(options.sortInterval, 0));
    static_cast<void>(finalCounters);
}

Options parseOptions(int argumentCount, char** arguments) {
    Options options;
    for (int i = 1; i + 1 < argumentCount; i += 2) {
        const std::string key = arguments[i];
        const std::string value = arguments[i + 1];
        if (key == "--dir") {
            options.levelDirectory = value;
        } else if (key == "--level") {
            options.levelName = value;
        } else if (key == "--api") {
            options.apis = value == "all" ? std::vector<std::string>{"d3d11", "d3d12", "vulkan"}
                                          : std::vector<std::string>{value};
        } else if (key == "--balls") {
            options.balls = std::strtoull(value.c_str(), nullptr, 10);
        } else if (key == "--ticks") {
            options.ticks = std::atoi(value.c_str());
        } else if (key == "--substeps") {
            options.substeps = std::atoi(value.c_str());
        } else if (key == "--sort-interval") {
            options.sortInterval = value == "auto" ? automaticSortInterval : std::atoi(value.c_str());
        } else if (key == "--step-group-balls") {
            options.ballsPerStepGroup = static_cast<uint32_t>(std::atoi(value.c_str()));
        } else if (key == "--count-group-balls") {
            options.ballsPerCountGroup = static_cast<uint32_t>(std::atoi(value.c_str()));
        } else if (key == "--sort-group-balls") {
            options.ballsPerSortGroup = static_cast<uint32_t>(std::atoi(value.c_str()));
        } else if (key == "--blob-dir") {
            options.blobDirectory = value;
        }
    }
    return options;
}

}

int main(int argumentCount, char** arguments) {
    const Options options = parseOptions(argumentCount, arguments);
    bool layoutOk = false;
    const BrickLayout layout = layoutFor(options, layoutOk);
    if (!layoutOk) {
        return 1;
    }
    const Workload work = buildWorkload(layout, options);
    for (const std::string& api : options.apis) {
        std::unique_ptr<Device> target = createDevice(api);
        if (!target) {
            std::printf("%-34s unavailable\n", api.c_str());
            continue;
        }
        loadBlobOverrides(*target, api, options.blobDirectory);
        runBench(*target, work, options);
        target->device = nullptr;
        target->release();
    }
    return 0;
}
