#pragma once

#include "common/BallCount.h"

#include "game/FrameTimings.h"
#include "game/SessionView.h"
#include "game/Settings.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"
#include "sim/BackendSelection.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class UiTone { Normal, Muted, Accent, Success, Danger };

struct UiOption {
    std::string label;
    bool enabled = true;
    bool selected = false;

    std::string detail;
};

namespace menuText {

inline constexpr std::string_view layoutHeading = "Layout";
inline constexpr std::string_view layoutHint = "Auto follows the window shape.";
inline constexpr std::string_view wideScreenLabel = "Wide screens";
inline constexpr std::string_view wideScreenHint = "Wider than 16:9: fill to the edges, or keep 16:9 centered.";
inline constexpr std::string_view displayHeading = "Display";
inline constexpr std::string_view vsyncLabel = "VSync";
inline constexpr std::string_view physicsHeading = "Physics";
inline constexpr std::string_view defaultBackendLabel = "Default backend";
inline constexpr std::string_view runBenchmarkLabel = "Run machine benchmark";
inline constexpr std::string_view devicesLabel = "Devices";
inline constexpr std::string_view devicesHint =
    "The devices Hybrid splits the balls between, one backend each. Leaving one device on runs that backend alone.";
inline constexpr std::string_view advancedSettingsLabel = "Advanced Settings";
inline constexpr std::string_view releaseLabel = "Release";
inline constexpr std::string_view spawnRateLabel = "Spawn / sec";
inline constexpr std::string_view ballLimitLabel = "Ball limit";
inline constexpr std::string_view renderGpuLabel = "Render GPU";
inline constexpr std::string_view computeGpuLabel = "Compute GPU (Vulkan backend)";
inline constexpr std::string_view ballCollisionsLabel = "Ball collisions";
inline constexpr std::string_view allLaunchersReleased = "All launchers released";

}

std::string brickHitLabel(int64_t hitsLeft);

std::vector<UiOption> orientationOptions(LayoutOrientation current);

std::vector<UiOption> wideScreenOptions(WideScreenAnchor current);
std::vector<UiOption> offOnOptions(bool on);

struct BackendRow {
    BackendKind kind = BackendKind::Scalar;
    UiOption option;
};

std::vector<BackendRow> defaultBackendRows(BackendKind preferred, std::span<const CandidateResult> scores);

struct DevicePickRow {
    std::string device;
    std::string label;

    std::vector<UiOption> options;
    std::vector<std::optional<BackendKind>> kinds;
};

inline bool showsDevicePicks(BackendKind selected) { return selected == BackendKind::Hybrid; }

std::vector<DevicePickRow> devicePickRows(std::span<const BackendPick> current);

std::optional<std::vector<BackendPick>> pickDeviceOption(std::span<const BackendPick> current,
                                                         std::span<const DevicePickRow> rows, size_t row,
                                                         size_t option);

UiOption releaseButton(const SessionView& view);
std::vector<UiOption> spawnRateChoices(int selectedIndex);

std::vector<UiOption> ballLimitChoices(const SessionView& view);

std::string waitingToSpawnLabel(const SessionView& view);
bool isAutoBallLimit(const SessionView& view);

std::vector<BackendRow> gameplayBackendRows(const SessionView& view);

struct BallCollisionsChoice {
    std::vector<UiOption> options;
    std::string hint;
};
BallCollisionsChoice ballCollisionsChoice(bool requested);

bool ballCollisionsOffered();

bool backendChoiceOffered();

struct DebugRow {
    enum class Kind { Heading, Value, Color };

    Kind kind = Kind::Value;
    std::string label;
    std::string value;
    UiTone tone = UiTone::Normal;
    int colorIndex = 0;
};

struct DebugGadgetOptions {

    bool showBackend = false;

    std::string renderer;

    std::vector<std::pair<std::string, std::string>> portRows;
};

std::vector<DebugRow> debugGadgetRows(const SessionView& view, const FrameTimings& frames,
                                      const DebugGadgetOptions& options);

std::string backendFailedMessage(BackendKind kind);
std::string physicsOnMessage(std::span<const BackendPick> picks);
std::string computeGpuMessage(std::string_view gpuName);
