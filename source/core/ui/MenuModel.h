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

// The rows and texts of the game's Settings panel, gameplay control gadget and debug gadget, built once for every
// front end. Each list is what the design shows, in its order, with the labels, which option is selected and which are
// disabled with why. Front ends draw these as their own widgets and apply a click to the session or settings as
// before; rows only one port has (SDL's Renderer and Graphics API, Unreal's Graphics quality, Unity's adapter
// readout) stay in that port, added around these.

// How a value is colored: the theme's text, muted text, accent, success or danger.
enum class UiTone { Normal, Muted, Accent, Success, Danger };

struct UiOption {
    std::string label;
    bool enabled = true;
    bool selected = false;
    // Muted text beside or under the option: a benchmark score, or why it is disabled.
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

} // namespace menuText

// A brick's hits left as its label: whole below 10,000, then three significant digits with K, M or B ("12.4K").
std::string brickHitLabel(int64_t hitsLeft);

// Settings panel.
std::vector<UiOption> orientationOptions(LayoutOrientation current);
// "Screen edges", then "Centered 16:9", in WideScreenAnchor order.
std::vector<UiOption> wideScreenOptions(WideScreenAnchor current);
std::vector<UiOption> offOnOptions(bool on);

struct BackendRow {
    BackendKind kind = BackendKind::Scalar;
    UiOption option;
};

// The Default backend list: every selectable backend, disabled with its reason where it cannot load, with its last
// machine benchmark score as the detail.
std::vector<BackendRow> defaultBackendRows(BackendKind preferred, std::span<const CandidateResult> scores);

struct DevicePickRow {
    std::string device;  // PhysicalDevice::id
    std::string label;
    // Off first, then one option per backend the device runs.
    std::vector<UiOption> options;
    std::vector<std::optional<BackendKind>> kinds;
};

// The Devices rows belong to Hybrid: they show only while it is the selected backend.
inline bool showsDevicePicks(BackendKind selected) { return selected == BackendKind::Hybrid; }
// One row per physical device for the picks shown; the last device on cannot be turned off.
std::vector<DevicePickRow> devicePickRows(std::span<const BackendPick> current);
// The picks after a click on option `option` of row `row`, or nothing when the click changes nothing.
std::optional<std::vector<BackendPick>> pickDeviceOption(std::span<const BackendPick> current,
                                                         std::span<const DevicePickRow> rows, size_t row,
                                                         size_t option);

// Gameplay control gadget.
UiOption releaseButton(const SessionView& view);
std::vector<UiOption> spawnRateChoices(int selectedIndex);
// Level, Auto, Max (BallLimitChoice order). Level releases the level's counts; Auto and Max keep releasing until the
// limit (see BallLimitChoice).
std::vector<UiOption> ballLimitChoices(const SessionView& view);
bool isAutoBallLimit(const SessionView& view);
// The in-game Physics list: like the default list, a backend still starting reads "<name> - starting...".
std::vector<BackendRow> gameplayBackendRows(const SessionView& view);

struct BallCollisionsChoice {
    std::vector<UiOption> options;  // Off, On
    std::string hint;
};
BallCollisionsChoice ballCollisionsChoice(bool requested);
// Ball collisions run only on Scalar. Where Scalar is not offered (the scoring build, or any machine with a faster
// backend) the option could never turn on, so front ends leave it, and its debug row, out.
bool ballCollisionsOffered();
// A single selectable backend cannot be changed, so the choice is pointless to show.
bool backendChoiceOffered();

// Debug gadget.
struct DebugRow {
    enum class Kind { Heading, Value, Color };

    Kind kind = Kind::Value;
    std::string label;
    std::string value;
    UiTone tone = UiTone::Normal;
    int colorIndex = 0;
};

struct DebugGadgetOptions {
    // Advanced Settings open: the backend, device or renderer, and ball collisions rows show.
    bool showBackend = false;
    // The game's renderer row ("WebGPU / Vulkan / RTX 3070"), when the port has one.
    std::string renderer;
    // The port's own timing rows, shown after the frame rows (SDL: CPU, Present; Unreal: Game thread...).
    std::vector<std::pair<std::string, std::string>> portRows;
};

std::vector<DebugRow> debugGadgetRows(const SessionView& view, const FrameTimings& frames,
                                      const DebugGadgetOptions& options);

// Toasts the gameplay screen shows for session events.
std::string backendFailedMessage(BackendKind kind);
std::string physicsOnMessage(std::span<const BackendPick> picks);
std::string computeGpuMessage(std::string_view gpuName);
