#pragma once

#include "sim/BackendFactory.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct PhysicalDevice;

// Which backend runs on which physical device. One pick runs that backend alone on that device; two or more run
// the device composer (BackendKind::Hybrid) with one part per pick. Each device takes at most one pick.
//
// Text form, used by Settings, the command line (--backend) and the control API (/backend?kind=):
//   "AVX2@cpu+CUDA@gpu0+Vulkan@gpu1"
// Kinds are matched like backendKindFromName; devices like findPhysicalDevice (id, UUID prefix or name substring),
// so "vulkan@6700" works. The canonical form written back uses device ids.
struct BackendPick {
    BackendKind kind = BackendKind::Scalar;
    std::string device;  // PhysicalDevice::id

    bool operator==(const BackendPick&) const = default;
};

// Every pick names a device and a kind that device runs. Nullopt on any unknown kind or device.
std::optional<std::vector<BackendPick>> parseBackendPicks(std::string_view text);
std::string formatBackendPicks(std::span<const BackendPick> picks);
// "AVX2 (CPU) + CUDA (RTX 2060) + Vulkan (RX 6700 XT)" for menus and the debug panel.
std::string backendPicksLabel(std::span<const BackendPick> picks);
// Empty when the picks can run here; otherwise why not (two picks on one device, a kind the device lacks, a
// backend that cannot load, Hybrid or CUDA+AVX2 picked as a part).
std::string backendPicksProblem(std::span<const BackendPick> picks);
// Hybrid for two or more picks, the pick's kind for one.
BackendKind kindForPicks(std::span<const BackendPick> picks);

// The process-wide picks: what Hybrid composes and which device a single kind runs on when it is picked. Empty by
// default: Hybrid then composes defaultHybridPicks() and each kind runs on its default device.
void setBackendPicks(std::vector<BackendPick> picks);
std::vector<BackendPick> backendPicks();
// The fastest available backend on the CPU and on every discrete GPU (CUDA where it loads, else Vulkan).
std::vector<BackendPick> defaultHybridPicks();
// What Hybrid runs now: the configured picks when there are two or more, else defaultHybridPicks().
std::vector<BackendPick> hybridPicks();
// The plugin device a single kind runs on: the configured pick for that kind, else the kind's default device.
// -1 when the kind has no device here.
int pluginDeviceFor(BackendKind kind);

BackendHandle createComposedBackend(std::span<const BackendPick> picks);

// What a per-device picker shows while `running` is the active kind: the configured picks if they describe it,
// otherwise the picks Hybrid would compose or the single kind on its default device.
std::vector<BackendPick> displayedPicks(BackendKind running);
// The kinds a device row offers, fastest first. Like selectableBackends, Scalar is offered on the CPU only when
// nothing faster runs there.
std::vector<BackendKind> pickableKinds(const PhysicalDevice& device);
// `current` with `device` running `kind`, or with the device left out when kind is empty.
std::vector<BackendPick> withDevicePick(std::span<const BackendPick> current, std::string_view device,
                                        std::optional<BackendKind> kind);

// Settings and saved profiles store the composer by its picks ("AVX2@cpu+CUDA@gpu0"), never as "Hybrid": that
// word in a saved file predates the composer and meant the CUDA+AVX2 pair.
struct StoredBackend {
    BackendKind kind = BackendKind::Scalar;
    std::vector<BackendPick> picks;  // set when the text was a picks list
};
std::optional<StoredBackend> parseStoredBackend(std::string_view text);
std::string storedBackendName(BackendKind kind);
