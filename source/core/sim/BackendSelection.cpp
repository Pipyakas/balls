#include "sim/BackendSelection.h"

#include "sim/DeviceComposer.h"
#include "sim/DeviceInventory.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>

namespace {

constexpr char pickSeparator = '+';
constexpr char deviceSeparator = '@';
constexpr std::string_view legacyCudaAvx2Name = "hybrid";
constexpr double cpuPartWeight = 0.1;
constexpr double integratedGpuPartWeight = 0.2;
constexpr double vulkanPartWeight = 0.8;
constexpr double cudaPartWeight = 1.0;

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
    for (const std::string_view noise : {"NVIDIA ", "GeForce ", "AMD ", "Radeon(TM) ", "Radeon ", "(TM)", "Intel(R) "}) {
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
    return kind == BackendKind::Cuda ? cudaPartWeight : vulkanPartWeight;
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
        const PhysicalDevice* device = findPhysicalDevice(item.substr(at + 1));
        if (!kind || !device) {
            return std::nullopt;
        }
        picks.push_back({*kind, device->id});
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

std::string backendPicksProblem(std::span<const BackendPick> picks) {
    if (picks.empty()) {
        return "no device picked";
    }
    for (size_t index = 0; index < picks.size(); ++index) {
        const BackendPick& pick = picks[index];
        if (!isPartKind(pick.kind)) {
            return std::format("{} is not a single-device backend", backendKindName(pick.kind));
        }
        const PhysicalDevice* device = findPhysicalDevice(pick.device);
        if (!device) {
            return std::format("no device '{}'", pick.device);
        }
        for (size_t other = 0; other < index; ++other) {
            if (picks[other].device == device->id) {
                return std::format("{} has two backends picked; each device runs one", shortDeviceName(*device));
            }
        }
        const DeviceBackendOption* option = device->option(pick.kind);
        if (!option) {
            return std::format("{} cannot run on {}", backendKindName(pick.kind), shortDeviceName(*device));
        }
        if (const std::string reason = backendUnavailableReasonOnDevice(pick.kind, option->pluginDevice); !reason.empty()) {
            return std::format("{} on {}: {}", backendKindName(pick.kind), shortDeviceName(*device), reason);
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
        const PhysicalDevice& device = *findPhysicalDevice(pick.device);
        BackendHandle backend = createBackendOnDevice(pick.kind, device.option(pick.kind)->pluginDevice);
        if (!backend) {
            return BackendHandle(nullptr, BackendRelease{});
        }
        parts.push_back({std::move(backend), std::format("{}@{}", backendKindName(pick.kind), device.id),
                         partWeight(pick.kind, device)});
    }
    if (parts.size() == 1) {
        return std::move(parts.front().backend);
    }
    return DeviceComposer::create(std::move(parts));
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
    return kind == BackendKind::Hybrid ? formatBackendPicks(hybridPicks()) : std::string(backendKindName(kind));
}
