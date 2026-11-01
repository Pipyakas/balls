#pragma once

#include "sim/BackendFactory.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct PhysicalDevice;

struct BackendPick {
    BackendKind kind = BackendKind::Scalar;
    std::string device;

    bool operator==(const BackendPick&) const = default;
};

std::optional<std::vector<BackendPick>> parseBackendPicks(std::string_view text);
std::string formatBackendPicks(std::span<const BackendPick> picks);

std::string backendPicksLabel(std::span<const BackendPick> picks);

std::string backendPicksProblem(std::span<const BackendPick> picks);

BackendKind kindForPicks(std::span<const BackendPick> picks);

void setBackendPicks(std::vector<BackendPick> picks);
std::vector<BackendPick> backendPicks();

std::vector<BackendPick> defaultHybridPicks();

std::vector<BackendPick> hybridPicks();

int pluginDeviceFor(BackendKind kind);

BackendHandle createComposedBackend(std::span<const BackendPick> picks);

std::vector<BackendPick> displayedPicks(BackendKind running);

std::vector<BackendKind> pickableKinds(const PhysicalDevice& device);

std::vector<BackendPick> withDevicePick(std::span<const BackendPick> current, std::string_view device,
                                        std::optional<BackendKind> kind);

struct StoredBackend {
    BackendKind kind = BackendKind::Scalar;
    std::vector<BackendPick> picks;
};
std::optional<StoredBackend> parseStoredBackend(std::string_view text);
std::string storedBackendName(BackendKind kind);

std::optional<BackendKind> parseBackendKind(std::string_view text);

std::optional<BackendKind> parseSavedBackendKind(std::string_view text);
