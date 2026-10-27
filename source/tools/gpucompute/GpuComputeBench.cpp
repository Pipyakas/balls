#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"
#include "sim/SortSchedule.h"
#include "sim/TargetField.h"
#include "common/DynamicLibrary.h"
#include "gpu/GpuAdapters.h"

#include "BallStepVariants.h"
#include "generated/BallStepBlobs.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <nvrhi/d3d11.h>
#include <nvrhi/d3d12.h>
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <nvrhi/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
#endif
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
    std::string levelDirectory = "data/assets/levels/performance";
    std::string levelName = "DA_1000Bricks";
#if defined(_WIN32)
    std::vector<std::string> apis{"d3d11", "d3d12", "vulkan"};
#else
    std::vector<std::string> apis{"vulkan"};
#endif
    size_t balls = 16'000'000;
    int ticks = 200;
    int substeps = 2;
    int sortInterval = automaticSortInterval;
    uint32_t ballsPerStepGroup = 0;
    uint32_t ballsPerCountGroup = 0;
    uint32_t ballsPerSortGroup = 0;
    bool hasStepGroupOverride = false;
    bool hasCountGroupOverride = false;
    bool hasSortGroupOverride = false;
    std::string gpuPreference = "auto";
    std::string profile;
    std::array<uint32_t, 6> profileNumbers{};
    bool hasProfileNumbers = false;
    bool listGpus = false;
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
    GpuAdapterInfo adapter;
    BallStepTuning tuning;
    nvrhi::DeviceHandle device;
    ShaderSet shaders;
    std::function<void()> release;
};

ShaderSet shaderSet(const BallStepBlobSet& blobs) {
    return {{blobs.ballStep.begin(), blobs.ballStep.end()},
            {blobs.countCells.begin(), blobs.countCells.end()},
            {blobs.scanCells.begin(), blobs.scanCells.end()},
            {blobs.stepAndSort.begin(), blobs.stepAndSort.end()}};
}

BallStepTuning tuningForAdapter(const GpuAdapterInfo& adapter, const Options& options) {
    BallStepTuning tuning;
    const std::span<const BallStepTuning> profiles = ballStepProfiles();
    if (options.profile.empty()) {
        tuning = ballStepTuningFor(adapter);
    } else {
        const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const BallStepTuning& candidate) {
            return candidate.variant == std::string_view(options.profile);
        });
        if (profile == profiles.end()) {
            const BallStepTuning& fallback = profiles.front();
            std::fprintf(stderr, "warning: unknown profile '%s'; using %.*s\n", options.profile.c_str(),
                         static_cast<int>(fallback.variant.size()), fallback.variant.data());
            tuning = fallback;
        } else {
            tuning = *profile;
        }
    }
    if (options.hasProfileNumbers) {
        tuning.threadsPerGroup = options.profileNumbers[0];
        tuning.ballsPerThread = options.profileNumbers[1];
        tuning.countThreads = options.profileNumbers[2];
        tuning.countItems = options.profileNumbers[3];
        tuning.sortThreads = options.profileNumbers[4];
        tuning.sortItems = options.profileNumbers[5];
    }
    return tuning;
}

#if defined(_WIN32)
bool selectD3dAdapter(const Options& options, GpuAdapterInfo& selected, ComPtr<IDXGIAdapter1>& adapter) {
    const std::vector<GpuAdapterInfo> adapters = enumerateGpuAdapters(GpuApi::D3d12);
    const int selectedPosition = selectGpuAdapter(adapters, options.gpuPreference, GpuRole::Compute);
    if (selectedPosition < 0) {
        return false;
    }
    selected = adapters[static_cast<size_t>(selectedPosition)];
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    // GpuAdapters.cpp enumerates DXGI adapters in EnumAdapters1 order, so the selected index maps directly.
    if (FAILED(factory->EnumAdapters1(static_cast<UINT>(selected.index), &adapter))) {
        return false;
    }
    return true;
}

std::unique_ptr<Device> createD3d12(const Options& options) {
    auto result = std::make_unique<Device>();
    GpuAdapterInfo adapterInfo;
    ComPtr<IDXGIAdapter1> adapter;
    if (!selectD3dAdapter(options, adapterInfo, adapter)) {
        return nullptr;
    }
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
    result->adapter = adapterInfo;
    result->tuning = tuningForAdapter(adapterInfo, options);
    result->name = "D3D12 " + adapterInfo.name;
    result->shaders = shaderSet(ballStepDxilSet(result->tuning.variant));
    result->release = [device, queue] {};
    return result->device ? std::move(result) : nullptr;
}

std::unique_ptr<Device> createD3d11(const Options& options) {
    auto result = std::make_unique<Device>();
    GpuAdapterInfo adapterInfo;
    ComPtr<IDXGIAdapter1> adapter;
    if (!selectD3dAdapter(options, adapterInfo, adapter)) {
        return nullptr;
    }
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
    result->adapter = adapterInfo;
    result->tuning = tuningForAdapter(adapterInfo, options);
    result->name = "D3D11 " + adapterInfo.name;
    result->shaders = shaderSet(ballStepDxbc, countCellsDxbc, scanCellsDxbc, stepAndSortDxbc);
    result->release = [device, context] {};
    return result->device ? std::move(result) : nullptr;
}
#endif

auto& vk() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

#if defined(_WIN32)
constexpr const char* vulkanLibraryName = "vulkan-1.dll";
#elif defined(__APPLE__)
constexpr const char* vulkanLibraryName = "libvulkan.1.dylib";
#else
constexpr const char* vulkanLibraryName = "libvulkan.so.1";
#endif

std::unique_ptr<Device> createVulkan(const Options& options) {
    auto loader = std::make_shared<DynamicLibrary>(vulkanLibraryName);
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    if (!loader->resolve("vkGetInstanceProcAddr", getInstanceProcAddr) || !getInstanceProcAddr) {
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

    const std::vector<GpuAdapterInfo> adapters = enumerateGpuAdapters(GpuApi::Vulkan);
    const int selectedIndex = selectGpuAdapter(adapters, options.gpuPreference, GpuRole::Compute);
    if (selectedIndex < 0) {
        vk().vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
    const GpuAdapterInfo adapterInfo = adapters[static_cast<size_t>(selectedIndex)];

    uint32_t count = 0;
    if (vk().vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) {
        vk().vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
    std::vector<VkPhysicalDevice> candidates(count);
    if (vk().vkEnumeratePhysicalDevices(instance, &count, candidates.data()) != VK_SUCCESS) {
        vk().vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
    candidates.resize(count);
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkPhysicalDeviceProperties selectedProperties{};
    for (VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties properties{};
        vk().vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.vendorID == adapterInfo.vendorId && properties.deviceID == adapterInfo.deviceId) {
            physicalDevice = candidate;
            selectedProperties = properties;
            break;
        }
    }
    if (!physicalDevice && candidates.size() == adapters.size() && static_cast<size_t>(selectedIndex) < candidates.size()) {
        // If PCI IDs cannot identify the adapter, rely on both enumerators preserving Vulkan device order.
        physicalDevice = candidates[static_cast<size_t>(selectedIndex)];
        vk().vkGetPhysicalDeviceProperties(physicalDevice, &selectedProperties);
    }
    if (physicalDevice && selectedProperties.apiVersion >= VK_API_VERSION_1_3) {
        uint32_t familyCount = 0;
        vk().vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vk().vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        bool foundQueueFamily = false;
        for (uint32_t family = 0; family < familyCount; ++family) {
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[family].queueFlags & needed) == needed) {
                queueFamily = family;
                foundQueueFamily = true;
                break;
            }
        }
        if (!foundQueueFamily) {
            physicalDevice = VK_NULL_HANDLE;
        }
    } else {
        physicalDevice = VK_NULL_HANDLE;
    }
    if (!physicalDevice) {
        vk().vkDestroyInstance(instance, nullptr);
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
        vk().vkDestroyInstance(instance, nullptr);
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
    result->adapter = adapterInfo;
    result->tuning = tuningForAdapter(adapterInfo, options);
    result->name = "Vulkan " + adapterInfo.name;
    result->shaders = shaderSet(ballStepSpirvSet(result->tuning.variant));
    if (!result->device) {
        vk().vkDeviceWaitIdle(logicalDevice);
        vk().vkDestroyDevice(logicalDevice, nullptr);
        vk().vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
    result->release = [loader, instance, logicalDevice] {
        static_cast<void>(loader);
        vk().vkDeviceWaitIdle(logicalDevice);
        vk().vkDestroyDevice(logicalDevice, nullptr);
        vk().vkDestroyInstance(instance, nullptr);
    };
    return result;
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

std::unique_ptr<Device> createDevice(const std::string& api, const Options& options) {
#if defined(_WIN32)
    if (api == "d3d11") {
        return createD3d11(options);
    }
    if (api == "d3d12") {
        return createD3d12(options);
    }
#endif
    if (api == "vulkan") {
        return createVulkan(options);
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
    const std::vector<int32_t> nearBrick = board.nearBrickFlags();
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

    const uint32_t stepItemsPerGroup = options.hasStepGroupOverride
                                           ? options.ballsPerStepGroup
                                           : target.tuning.threadsPerGroup * target.tuning.ballsPerThread;
    const uint32_t countItemsPerGroup = options.hasCountGroupOverride
                                            ? options.ballsPerCountGroup
                                            : target.tuning.countThreads * target.tuning.countItems;
    const uint32_t sortItemsPerGroup = options.hasSortGroupOverride
                                           ? options.ballsPerSortGroup
                                           : target.tuning.sortThreads * target.tuning.sortItems;
    const DispatchShape stepShape = shapeFor(count, stepItemsPerGroup);
    const DispatchShape countShape = shapeFor(count, countItemsPerGroup);
    const DispatchShape sortShape = shapeFor(count, sortItemsPerGroup);
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

bool parsePositiveInteger(std::string_view text, uint32_t& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

bool parseProfileNumbers(std::string_view text, std::array<uint32_t, 6>& numbers) {
    std::array<uint32_t, 6> parsed{};
    size_t start = 0;
    for (size_t index = 0; index < parsed.size(); ++index) {
        const size_t comma = text.find(',', start);
        if ((index + 1 < parsed.size() && comma == std::string_view::npos) ||
            (index + 1 == parsed.size() && comma != std::string_view::npos)) {
            return false;
        }
        const size_t end = comma == std::string_view::npos ? text.size() : comma;
        if (!parsePositiveInteger(text.substr(start, end - start), parsed[index])) {
            return false;
        }
        start = comma == std::string_view::npos ? text.size() : comma + 1;
    }
    const uint64_t maximum = std::numeric_limits<uint32_t>::max();
    if (static_cast<uint64_t>(parsed[0]) * parsed[1] > maximum ||
        static_cast<uint64_t>(parsed[2]) * parsed[3] > maximum ||
        static_cast<uint64_t>(parsed[4]) * parsed[5] > maximum) {
        return false;
    }
    numbers = parsed;
    return true;
}

const char* apiName(GpuApi api) {
    return api == GpuApi::Vulkan ? "vulkan" : "d3d12";
}

void printGpuAdapters(GpuApi api) {
    for (const GpuAdapterInfo& adapter : enumerateGpuAdapters(api)) {
        std::printf("index=%d api=%s %s vendor=0x%04X:0x%04X subgroup=%u-%u shared=%u workgroup=%u deviceLocalMB=%llu\n",
                    adapter.index, apiName(api), adapter.summary().c_str(), adapter.vendorId, adapter.deviceId,
                    adapter.subgroupMin, adapter.subgroupMax, adapter.maxSharedMemoryBytes,
                    adapter.maxWorkgroupInvocations,
                    static_cast<unsigned long long>(adapter.deviceLocalBytes / (1024 * 1024)));
    }
}

void printRunProfile(const Device& target, const std::string& api) {
    const std::string summary = target.adapter.summary();
    const std::string_view variant = target.tuning.variant;
    std::printf("adapter %s api %s profile %.*s (%u,%u,%u,%u,%u,%u)\n", summary.c_str(), api.c_str(),
                static_cast<int>(variant.size()), variant.data(), target.tuning.threadsPerGroup,
                target.tuning.ballsPerThread, target.tuning.countThreads, target.tuning.countItems,
                target.tuning.sortThreads, target.tuning.sortItems);
}

Options parseOptions(int argumentCount, char** arguments) {
    Options options;
    for (int i = 1; i < argumentCount; ++i) {
        const std::string key = arguments[i];
        if (key == "--list-gpus") {
            options.listGpus = true;
            continue;
        }
        if (i + 1 >= argumentCount || std::string_view(arguments[i + 1]).starts_with("--")) {
            continue;
        }
        const std::string value = arguments[++i];
        if (key == "--dir") {
            options.levelDirectory = value;
        } else if (key == "--level") {
            options.levelName = value;
        } else if (key == "--api") {
            if (value == "all") {
#if defined(_WIN32)
                options.apis = {"d3d11", "d3d12", "vulkan"};
#else
                options.apis = {"vulkan"};
#endif
            } else {
                options.apis = {value};
            }
        } else if (key == "--balls") {
            options.balls = std::strtoull(value.c_str(), nullptr, 10);
        } else if (key == "--ticks") {
            options.ticks = std::atoi(value.c_str());
        } else if (key == "--substeps") {
            options.substeps = std::atoi(value.c_str());
        } else if (key == "--sort-interval") {
            options.sortInterval = value == "auto" ? automaticSortInterval : std::atoi(value.c_str());
        } else if (key == "--step-group-balls") {
            if (parsePositiveInteger(value, options.ballsPerStepGroup)) {
                options.hasStepGroupOverride = true;
            }
        } else if (key == "--count-group-balls") {
            if (parsePositiveInteger(value, options.ballsPerCountGroup)) {
                options.hasCountGroupOverride = true;
            }
        } else if (key == "--sort-group-balls") {
            if (parsePositiveInteger(value, options.ballsPerSortGroup)) {
                options.hasSortGroupOverride = true;
            }
        } else if (key == "--gpu") {
            options.gpuPreference = value;
        } else if (key == "--profile") {
            options.profile = value;
        } else if (key == "--profile-numbers") {
            if (parseProfileNumbers(value, options.profileNumbers)) {
                options.hasProfileNumbers = true;
            } else {
                std::fprintf(stderr, "warning: invalid --profile-numbers; expected six positive integers\n");
            }
        } else if (key == "--blob-dir") {
            options.blobDirectory = value;
        }
    }
    return options;
}

}

int main(int argumentCount, char** arguments) {
    const Options options = parseOptions(argumentCount, arguments);
    if (options.listGpus) {
        printGpuAdapters(GpuApi::Vulkan);
#if defined(_WIN32)
        printGpuAdapters(GpuApi::D3d12);
#endif
        return 0;
    }
    if (!options.blobDirectory.empty() && !options.hasProfileNumbers) {
        std::fprintf(stderr, "warning: --blob-dir used without --profile-numbers; dispatch sizes may not match override blobs\n");
    }
    bool layoutOk = false;
    const BrickLayout layout = layoutFor(options, layoutOk);
    if (!layoutOk) {
        return 1;
    }
    const Workload work = buildWorkload(layout, options);
    for (const std::string& api : options.apis) {
        std::unique_ptr<Device> target = createDevice(api, options);
        if (!target) {
            std::printf("%-34s unavailable\n", api.c_str());
            continue;
        }
        loadBlobOverrides(*target, api, options.blobDirectory);
        printRunProfile(*target, api);
        runBench(*target, work, options);
        target->device = nullptr;
        target->release();
    }
    return 0;
}
