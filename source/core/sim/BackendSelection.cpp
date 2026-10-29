#include "sim/BackendSelection.h"

#include "common/Environment.h"
#include "sim/BackendWorker.h"
#include "sim/DeviceComposer.h"
#include "sim/DeviceInventory.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <mutex>

namespace {

constexpr char pickSeparator = '+';
constexpr char deviceSeparator = '@';
constexpr std::string_view legacyCudaAvx2Name = "hybrid";
// Starting shares only; measured throughput takes over within a few steps. A CPU steps about a twentieth of a
// discrete GPU's balls, and starting it with too many makes its first steps slow for every part.
constexpr double cpuPartWeight = 0.05;
constexpr double integratedGpuPartWeight = 0.2;
constexpr double vulkanPartWeight = 0.8;
constexpr double cudaPartWeight = 1.0;
// Device memory per ball a GPU backend needs at most (two 8-byte state copies), for memoryCapacity.
constexpr uint64_t gpuBytesPerBall = 16;

std::mutex& picksMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<BackendPick>& configuredPicks() {
    static std::vector<BackendPick> picks;
    return picks;
}

std::string_view trimmedView(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string shortDeviceName(const PhysicalDevice& device) {
    if (device.type == PhysicsDeviceType::Cpu) {
        return "CPU";
    }
    std::string name = device.name;
    for (const std::string_view noise : {"NVIDIA ", "GeForce ", "AMD ", "(TM)", "(R)"}) {
        if (const size_t found = name.find(noise); found != std::string::npos) {
            name.erase(found, noise.size());
        }
    }
    return name.empty() ? device.name : name;
}

double partWeight(BackendKind kind, const PhysicalDevice& device) {
    if (device.type == PhysicsDeviceType::Cpu) {
        return cpuPartWeight;
    }
    if (device.type == PhysicsDeviceType::IntegratedGpu) {
        return integratedGpuPartWeight;
    }
    return kind == BackendKind::Cuda || kind == BackendKind::Rocm ? cudaPartWeight : vulkanPartWeight;
}

bool isPartKind(BackendKind kind) { return kind != BackendKind::Hybrid && kind != BackendKind::CudaAvx2; }

}

std::optional<std::vector<BackendPick>> parseBackendPicks(std::string_view text) {
    std::vector<BackendPick> picks;
    while (!trimmedView(text).empty()) {
        const size_t end = text.find(pickSeparator);
        const std::string_view item = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const size_t at = item.find(deviceSeparator);
        if (at == std::string_view::npos) {
            return std::nullopt;
        }
        const std::optional<BackendKind> kind = backendKindFromName(trimmedView(item.substr(0, at)));
        if (!kind) {
            return std::nullopt;
        }
        std::string deviceToken = std::string(trimmedView(item.substr(at + 1)));
        if (*kind == BackendKind::VulkanMulti) {
            // A VulkanMulti pick may claim several devices separated by '|'.
            std::vector<std::string> claimed;
            size_t pos = 0;
            while (true) {
                const size_t next = deviceToken.find('|', pos);
                const std::string token = deviceToken.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
                const PhysicalDevice* device = findPhysicalDevice(trimmedView(token));
                if (!device) {
                    return std::nullopt;
                }
                claimed.push_back(device->id);
                if (next == std::string::npos) {
                    break;
                }
                pos = next + 1;
            }
            if (claimed.size() < 2) {
                return std::nullopt;
            }
            std::string joined;
            for (size_t i = 0; i < claimed.size(); ++i) {
                if (i) {
                    joined += '|';
                }
                joined += claimed[i];
            }
            picks.push_back({*kind, joined});
        } else {
            const PhysicalDevice* device = findPhysicalDevice(deviceToken);
            if (!device) {
                return std::nullopt;
            }
            picks.push_back({*kind, device->id});
        }
    }
    if (picks.empty()) {
        return std::nullopt;
    }
    return picks;
}

std::string formatBackendPicks(std::span<const BackendPick> picks) {
    std::string text;
    for (const BackendPick& pick : picks) {
        if (!text.empty()) {
            text += pickSeparator;
        }
        text += std::format("{}{}{}", backendKindName(pick.kind), deviceSeparator, pick.device);
    }
    return text;
}

std::string backendPicksLabel(std::span<const BackendPick> picks) {
    std::string label;
    for (const BackendPick& pick : picks) {
        if (!label.empty()) {
            label += " + ";
        }
        const PhysicalDevice* device = findPhysicalDevice(pick.device);
        label += std::format("{} ({})", backendKindName(pick.kind), device ? shortDeviceName(*device) : pick.device);
    }
    return label;
}

namespace {
std::vector<std::string> claimedDevices(const BackendPick& pick) {
    if (pick.kind != BackendKind::VulkanMulti) {
        return {pick.device};
    }
    std::vector<std::string> result;
    size_t pos = 0;
    while (true) {
        const size_t next = pick.device.find('|', pos);
        result.push_back(pick.device.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }
    return result;
}
}

std::string backendPicksProblem(std::span<const BackendPick> picks) {
    if (picks.empty()) {
        return "no device picked";
    }
    std::vector<std::string> usedDevices;
    for (size_t index = 0; index < picks.size(); ++index) {
        const BackendPick& pick = picks[index];
        if (!isPartKind(pick.kind)) {
            return std::format("{} is not a single-device backend", backendKindName(pick.kind));
        }
        const std::vector<std::string> claimed = claimedDevices(pick);
        if (claimed.empty()) {
            return std::format("no device '{}'", pick.device);
        }
        for (const std::string& token : claimed) {
            const PhysicalDevice* device = findPhysicalDevice(token);
            if (!device) {
                return std::format("no device '{}'", token);
            }
            for (const std::string& used : usedDevices) {
                if (used == device->id) {
                    return std::format("{} has two backends picked; each device runs one", shortDeviceName(*device));
                }
            }
            const BackendKind optionKind = pick.kind == BackendKind::VulkanMulti ? BackendKind::Vulkan : pick.kind;
            const DeviceBackendOption* option = device->option(optionKind);
            if (!option) {
                return std::format("{} cannot run on {}", backendKindName(pick.kind), shortDeviceName(*device));
            }
            if (const std::string reason = backendUnavailableReasonOnDevice(optionKind, option->pluginDevice); !reason.empty()) {
                return std::format("{} on {}: {}", backendKindName(pick.kind), shortDeviceName(*device), reason);
            }
            usedDevices.push_back(device->id);
        }
    }
    return {};
}

BackendKind kindForPicks(std::span<const BackendPick> picks) {
    return picks.size() == 1 ? picks.front().kind : BackendKind::Hybrid;
}

void setBackendPicks(std::vector<BackendPick> picks) {
    std::lock_guard lock(picksMutex());
    configuredPicks() = std::move(picks);
}

std::vector<BackendPick> backendPicks() {
    std::lock_guard lock(picksMutex());
    return configuredPicks();
}

std::vector<BackendPick> defaultHybridPicks() {
    std::vector<BackendPick> picks;
    for (const PhysicalDevice& device : physicalDevices()) {
        if (device.type != PhysicsDeviceType::Cpu && device.type != PhysicsDeviceType::DiscreteGpu) {
            continue;
        }
        for (const DeviceBackendOption& option : device.backends) {
            if (backendUnavailableReasonOnDevice(option.kind, option.pluginDevice).empty()) {
                picks.push_back({option.kind, device.id});
                break;
            }
        }
    }
    return picks;
}

std::vector<BackendPick> hybridPicks() {
    std::vector<BackendPick> picks = backendPicks();
    return picks.size() >= 2 ? picks : defaultHybridPicks();
}

int pluginDeviceFor(BackendKind kind) {
    for (const BackendPick& pick : backendPicks()) {
        if (pick.kind != kind) {
            continue;
        }
        if (const PhysicalDevice* device = findPhysicalDevice(pick.device)) {
            if (const DeviceBackendOption* option = device->option(kind)) {
                return option->pluginDevice;
            }
        }
    }
    const PhysicalDevice* device = defaultDeviceFor(kind);
    const DeviceBackendOption* option = device ? device->option(kind) : nullptr;
    return option ? option->pluginDevice : -1;
}

BackendHandle createComposedBackend(std::span<const BackendPick> picks) {
    if (!backendPicksProblem(picks).empty()) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    std::vector<DeviceComposer::PartSpec> parts;
    for (const BackendPick& pick : picks) {
        if (pick.kind == BackendKind::VulkanMulti) {
            std::vector<int> pluginDevices;
            size_t memoryBytes = 0;
            double weight = 0.0;
            const std::vector<std::string> claimed = claimedDevices(pick);
            for (const std::string& token : claimed) {
                const PhysicalDevice* device = findPhysicalDevice(token);
                if (!device || !device->option(BackendKind::Vulkan)) {
                    return BackendHandle(nullptr, BackendRelease{});
                }
                pluginDevices.push_back(device->option(BackendKind::Vulkan)->pluginDevice);
                memoryBytes += device->memoryBytes;
                weight += partWeight(BackendKind::Vulkan, *device);
            }
            BackendHandle backend = createBackendOnDevices(BackendKind::VulkanMulti, pluginDevices.data(),
                                                          static_cast<int>(pluginDevices.size()));
            if (!backend) {
                return BackendHandle(nullptr, BackendRelease{});
            }
            parts.push_back({std::move(backend), std::string("VulkanMulti"), weight,
                              memoryBytes > 0 ? memoryBytes / gpuBytesPerBall : size_t{0}});
            continue;
        }
        const PhysicalDevice& device = *findPhysicalDevice(pick.device);
        BackendHandle backend = createBackendOnDevice(pick.kind, device.option(pick.kind)->pluginDevice);
        if (!backend) {
            return BackendHandle(nullptr, BackendRelease{});
        }
        parts.push_back({std::move(backend), std::format("{}@{}", backendKindName(pick.kind), device.id),
                         partWeight(pick.kind, device), static_cast<size_t>(device.memoryBytes / gpuBytesPerBall)});
    }
    if (parts.size() == 1) {
        return std::move(parts.front().backend);
    }
    DeviceComposerTuning tuning;
    tuning.parkLosingParts = environmentVariable("BALLS_COMPOSER_NO_PARK") != "1";
    return DeviceComposer::create(std::move(parts), tuning);
}

std::vector<BackendPick> displayedPicks(BackendKind running) {
    std::vector<BackendPick> picks = backendPicks();
    if (!picks.empty() && kindForPicks(picks) == running) {
        return picks;
    }
    if (running == BackendKind::Hybrid) {
        return hybridPicks();
    }
    if (running == BackendKind::CudaAvx2) {
        return {};
    }
    for (const BackendPick& pick : picks) {
        if (pick.kind == running) {
            return {pick};
        }
    }
    const PhysicalDevice* device = defaultDeviceFor(running);
    return device ? std::vector<BackendPick>{{running, device->id}} : std::vector<BackendPick>{};
}

std::vector<BackendKind> pickableKinds(const PhysicalDevice& device) {
    std::vector<BackendKind> kinds;
    bool fasterCpuKind = false;
    for (const DeviceBackendOption& option : device.backends) {
        if (option.kind != BackendKind::Scalar && deviceBackendStatus(option.kind, option.pluginDevice).available) {
            fasterCpuKind = fasterCpuKind || device.type == PhysicsDeviceType::Cpu;
        }
    }
    for (const DeviceBackendOption& option : device.backends) {
        if (option.kind != BackendKind::Scalar || !fasterCpuKind) {
            kinds.push_back(option.kind);
        }
    }
    return kinds;
}

std::vector<BackendPick> withDevicePick(std::span<const BackendPick> current, std::string_view device,
                                        std::optional<BackendKind> kind) {
    std::vector<BackendPick> picks;
    bool placed = false;
    for (const BackendPick& pick : current) {
        if (pick.device != device) {
            picks.push_back(pick);
        } else if (kind) {
            picks.push_back({*kind, pick.device});
            placed = true;
        }
    }
    if (kind && !placed) {
        picks.push_back({*kind, std::string(device)});
    }
    // Keep device order stable (cpu, gpu0, gpu1, ...), whatever order the player clicked in.
    const std::vector<PhysicalDevice>& devices = physicalDevices();
    const auto rank = [&](const BackendPick& pick) {
        const auto found = std::ranges::find(devices, pick.device, &PhysicalDevice::id);
        return found - devices.begin();
    };
    std::ranges::stable_sort(picks, {}, rank);
    return picks;
}

std::optional<StoredBackend> parseStoredBackend(std::string_view text) {
    text = trimmedView(text);
    if (text.find(deviceSeparator) != std::string_view::npos) {
        std::optional<std::vector<BackendPick>> picks = parseBackendPicks(text);
        if (!picks) {
            return std::nullopt;
        }
        const BackendKind kind = kindForPicks(*picks);
        return StoredBackend{kind, std::move(*picks)};
    }
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (lowered == legacyCudaAvx2Name) {
        return StoredBackend{BackendKind::CudaAvx2, {}};
    }
    const std::optional<BackendKind> kind = backendKindFromName(text);
    if (!kind) {
        return std::nullopt;
    }
    return StoredBackend{*kind, {}};
}

std::string storedBackendName(BackendKind kind) {
    if (kind == BackendKind::CudaAvx2) {
        return std::string(backendKindName(kind));
    }
    const std::vector<BackendPick> picks = backendPicks();
    if (!picks.empty() && kindForPicks(picks) == kind) {
        return formatBackendPicks(picks);
    }
    return kind == BackendKind::Hybrid ? formatBackendPicks(hybridPicks()) : std::string(backendKindName(kind));
}

namespace {

std::optional<BackendKind> applyStoredBackend(const std::optional<StoredBackend>& stored) {
    if (!stored) {
        return std::nullopt;
    }
    if (!stored->picks.empty()) {
        if (!backendPicksProblem(stored->picks).empty()) {
            return std::nullopt;
        }
        setBackendPicks(stored->picks);
        refreshBackendStatuses();
    }
    return stored->kind;
}

} // namespace

std::optional<BackendKind> parseBackendKind(std::string_view text) {
    if (text.find('@') != std::string_view::npos) {
        return applyStoredBackend(parseStoredBackend(text));
    }
    return backendKindFromName(text);
}

std::optional<BackendKind> parseSavedBackendKind(std::string_view text) {
    return applyStoredBackend(parseStoredBackend(text));
}
