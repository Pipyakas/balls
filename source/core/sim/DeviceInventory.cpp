#include "sim/DeviceInventory.h"

#include "gpu/GpuAdapters.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <optional>
#include <tuple>

#if defined(_WIN32)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace {

constexpr size_t minimumUuidPrefixDigits = 8;

constexpr std::array<BackendKind, 6> inventoryKinds{BackendKind::Zen4, BackendKind::Avx2, BackendKind::Scalar,
                                                     BackendKind::Cuda, BackendKind::Rocm, BackendKind::Vulkan};

std::string lowercase(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

std::string trimmed(std::string text) {
    const auto first = text.find_first_not_of(' ');
    const auto last = text.find_last_not_of(' ');
    return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
}

struct GpuEntry {
    PhysicalDevice device;
    std::optional<std::tuple<int, int, int>> pciLocation;
    size_t firstSeen = 0;
};

}

const DeviceBackendOption* PhysicalDevice::option(BackendKind kind) const {
    const auto found = std::ranges::find(backends, kind, &DeviceBackendOption::kind);
    return found == backends.end() ? nullptr : &*found;
}

std::string uuidText(const std::array<uint8_t, 16>& uuid) {
    std::string text;
    for (const uint8_t byte : uuid) {
        text += std::format("{:02x}", byte);
    }
    return text;
}

std::string cpuBrandName() {
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
    constexpr unsigned firstBrandLeaf = 0x80000002u;
    constexpr unsigned brandLeafCount = 3;
    char brand[brandLeafCount * 16 + 1] = {};
    for (unsigned leaf = 0; leaf < brandLeafCount; ++leaf) {
        unsigned registers[4] = {};
#if defined(_WIN32)
        __cpuid(reinterpret_cast<int*>(registers), static_cast<int>(firstBrandLeaf + leaf));
#else
        __get_cpuid(firstBrandLeaf + leaf, &registers[0], &registers[1], &registers[2], &registers[3]);
#endif
        std::memcpy(brand + leaf * 16, registers, 16);
    }
    return trimmed(brand);
#else
    return "unknown CPU";
#endif
}

std::vector<PhysicalDevice> mergePhysicalDevices(
    const std::vector<std::pair<BackendKind, std::vector<PhysicsDeviceInfo>>>& kindDevices, std::string_view cpuName) {
    std::optional<PhysicalDevice> cpu;
    std::vector<GpuEntry> gpus;
    for (const auto& [kind, devices] : kindDevices) {
        for (size_t index = 0; index < devices.size(); ++index) {
            const PhysicsDeviceInfo& info = devices[index];
            const DeviceBackendOption option{kind, static_cast<int>(index)};
            if (info.type == PhysicsDeviceType::Cpu) {
                if (!cpu) {
                    cpu = PhysicalDevice{"cpu", std::string(cpuName), PhysicsDeviceType::Cpu};
                }
                cpu->backends.push_back(option);
                continue;
            }
            std::array<uint8_t, 16> uuid{};
            std::copy(std::begin(info.uuid), std::end(info.uuid), uuid.begin());

            const auto same = std::ranges::find_if(gpus, [&](const GpuEntry& entry) {
                if (info.hasUuid != 0 && entry.device.hasUuid && entry.device.uuid == uuid) {
                    return true;
                }
                return info.pciBus >= 0 && entry.pciLocation &&
                       *entry.pciLocation == std::tuple{info.pciDomain, info.pciBus, info.pciDevice};
            });
            if (same != gpus.end()) {
                same->device.backends.push_back(option);
                if (!same->pciLocation && info.pciBus >= 0) {
                    same->pciLocation = std::tuple{info.pciDomain, info.pciBus, info.pciDevice};
                }
                continue;
            }
            GpuEntry entry;
            entry.device.name = info.name;
            entry.device.type = info.type;
            entry.device.uuid = uuid;
            entry.device.hasUuid = info.hasUuid != 0;
            entry.device.memoryBytes = info.memoryBytes;
            entry.device.backends.push_back(option);
            if (info.pciBus >= 0) {
                entry.pciLocation = std::tuple{info.pciDomain, info.pciBus, info.pciDevice};
            }
            entry.firstSeen = gpus.size();
            gpus.push_back(std::move(entry));
        }
    }
    std::ranges::stable_sort(gpus, [](const GpuEntry& left, const GpuEntry& right) {
        if (left.pciLocation && right.pciLocation) {
            return *left.pciLocation < *right.pciLocation;
        }
        if (left.pciLocation.has_value() != right.pciLocation.has_value()) {
            return left.pciLocation.has_value();
        }
        return left.firstSeen < right.firstSeen;
    });
    std::vector<PhysicalDevice> devices;
    if (cpu) {
        devices.push_back(std::move(*cpu));
    }
    for (size_t index = 0; index < gpus.size(); ++index) {
        gpus[index].device.id = std::format("gpu{}", index);
        devices.push_back(std::move(gpus[index].device));
    }
    const auto rank = [](BackendKind kind) {
        return static_cast<int>(std::ranges::find(inventoryKinds, kind) - inventoryKinds.begin());
    };
    for (PhysicalDevice& device : devices) {
        std::ranges::stable_sort(device.backends, {}, [&](const DeviceBackendOption& option) { return rank(option.kind); });
    }
    return devices;
}

const std::vector<PhysicalDevice>& physicalDevices() {
    static const std::vector<PhysicalDevice> devices = [] {
        std::vector<std::pair<BackendKind, std::vector<PhysicsDeviceInfo>>> kindDevices;
        for (const BackendKind kind : inventoryKinds) {
            kindDevices.emplace_back(kind, backendDevices(kind));
        }
        return mergePhysicalDevices(kindDevices, cpuBrandName());
    }();
    return devices;
}

const PhysicalDevice* findPhysicalDevice(std::string_view token, const std::vector<PhysicalDevice>& devices) {
    const std::string wanted = lowercase(trimmed(std::string(token)));
    if (wanted.empty()) {
        return nullptr;
    }
    for (const PhysicalDevice& device : devices) {
        if (device.id == wanted) {
            return &device;
        }
    }
    if (wanted.size() >= minimumUuidPrefixDigits) {
        for (const PhysicalDevice& device : devices) {
            if (device.hasUuid && uuidText(device.uuid).starts_with(wanted)) {
                return &device;
            }
        }
    }
    for (const PhysicalDevice& device : devices) {
        if (lowercase(device.name).find(wanted) != std::string::npos) {
            return &device;
        }
    }
    return nullptr;
}

const PhysicalDevice* findPhysicalDevice(std::string_view token) { return findPhysicalDevice(token, physicalDevices()); }

const PhysicalDevice* defaultDeviceFor(BackendKind kind) {
    const std::vector<PhysicalDevice>& devices = physicalDevices();
    const auto runs = [kind](const PhysicalDevice& device) { return device.option(kind) != nullptr; };
    if (kind == BackendKind::Vulkan) {
        const std::vector<GpuAdapterInfo> adapters = enumerateGpuAdapters(GpuApi::Vulkan);
        const int selected = selectGpuAdapter(adapters, computeGpuPreference(), GpuRole::Compute);
        if (selected >= 0) {
            const GpuAdapterInfo& adapter = adapters[static_cast<size_t>(selected)];
            for (const PhysicalDevice& device : devices) {
                if (runs(device) && ((adapter.hasUuid && device.hasUuid && device.uuid == adapter.uuid) ||
                                     (!adapter.hasUuid && device.name == adapter.name))) {
                    return &device;
                }
            }
        }
    }
    if (kind == BackendKind::Cuda) {
        for (const PhysicalDevice& device : devices) {
            if (const DeviceBackendOption* option = device.option(kind); option && option->pluginDevice == 0) {
                return &device;
            }
        }
    }
    const auto found = std::ranges::find_if(devices, runs);
    return found == devices.end() ? nullptr : &*found;
}

std::string physicalDeviceLabel(const PhysicalDevice& device) {
    if (device.type == PhysicsDeviceType::Cpu) {
        return "CPU";
    }
    return device.type == PhysicsDeviceType::IntegratedGpu ? device.name + " (iGPU)" : device.name;
}
