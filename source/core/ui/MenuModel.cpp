#include "ui/MenuModel.h"

#include "sim/BackendWorker.h"
#include "sim/DeviceInventory.h"

#include <algorithm>
#include <array>
#include <format>

namespace {

constexpr double fpsTarget = 60.0;
constexpr double ballStepsPerMillion = 1e6;

UiTone fpsTone(double fps) { return fps >= fpsTarget ? UiTone::Success : UiTone::Danger; }

constexpr double tickRateTolerance = 0.95;

UiTone physicsTone(const SessionView& view) {
    const bool behind = view.state == SessionState::Running && view.targetTicksPerSecond > 0.0 &&
                        view.physicsTicksPerSecond < view.targetTicksPerSecond * tickRateTolerance;
    return behind ? UiTone::Danger : UiTone::Normal;
}

std::string spawnRateOptionLabel(int rate) {
    if (rate < 10'000) {
        return std::to_string(rate);
    }
    return rate >= 1'000'000 ? std::format("{}M", rate / 1'000'000) : std::format("{}k", rate / 1'000);
}

std::string benchmarkScore(std::span<const CandidateResult> scores, BackendKind kind) {
    const std::string shown = kind == BackendKind::Hybrid ? formatBackendPicks(displayedPicks(kind)) : std::string{};
    const auto found = std::ranges::find_if(scores, [&](const CandidateResult& result) {
        return result.candidate.kind == kind && result.candidate.picks == shown;
    });
    if (found == scores.end()) {
        return {};
    }
    switch (found->status) {
    case CandidateStatus::Done:
        return found->estimatedBalls > 0.0 ? std::format("{} balls", shortBallCount(found->estimatedBalls))
                                           : std::format("{:.0f} M/s", found->ballStepsPerSecond / ballStepsPerMillion);
    case CandidateStatus::Failed:
        return "failed";
    default:
        return {};
    }
}

class DebugRows {
public:
    void heading(std::string label) { rows.push_back({DebugRow::Kind::Heading, std::move(label)}); }
    void value(std::string label, std::string value, UiTone tone = UiTone::Normal) {
        rows.push_back({DebugRow::Kind::Value, std::move(label), std::move(value), tone});
    }
    void color(int colorIndex, std::string value) {
        rows.push_back({DebugRow::Kind::Color, std::string(ballColorName(ballColorFromIndex(colorIndex))),
                        std::move(value), UiTone::Normal, colorIndex});
    }

    std::vector<DebugRow> rows;
};

}

std::string brickHitLabel(int64_t hitsLeft) {
    constexpr int64_t wholeBelow = 10'000;
    if (hitsLeft < wholeBelow) {
        return std::to_string(hitsLeft);
    }
    constexpr std::array<std::pair<double, std::string_view>, 3> units{{{1e9, "B"}, {1e6, "M"}, {1e3, "K"}}};
    for (const auto& [scale, suffix] : units) {
        const double value = static_cast<double>(hitsLeft) / scale;
        if (value >= 1.0) {
            const int decimals = value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2;
            std::string text = std::format("{:.{}f}", value, decimals);
            if (text.find('.') != std::string::npos) {
                text.erase(text.find_last_not_of('0') + 1);
                if (text.back() == '.') {
                    text.pop_back();
                }
            }
            return text + std::string(suffix);
        }
    }
    return std::to_string(hitsLeft);
}

std::vector<UiOption> orientationOptions(LayoutOrientation current) {
    return {{"Auto", true, current == LayoutOrientation::Auto},
            {"Landscape", true, current == LayoutOrientation::Landscape},
            {"Portrait", true, current == LayoutOrientation::Portrait}};
}

std::vector<UiOption> wideScreenOptions(WideScreenAnchor current) {
    return {{"Screen edges", true, current == WideScreenAnchor::ScreenEdges},
            {"Centered 16:9", true, current == WideScreenAnchor::Centered}};
}

std::vector<UiOption> offOnOptions(bool on) { return {{"Off", true, !on}, {"On", true, on}}; }

std::vector<BackendRow> defaultBackendRows(BackendKind preferred, std::span<const CandidateResult> scores) {
    std::vector<BackendRow> rows;
    for (const BackendKind kind : selectableBackends()) {
        const BackendStatus status = backendStatus(kind);
        BackendRow row{kind, {std::string(backendKindName(kind)), status.available, preferred == kind}};
        row.option.detail = status.available ? benchmarkScore(scores, kind) : status.unavailableReason;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<DevicePickRow> devicePickRows(std::span<const BackendPick> current) {
    std::vector<DevicePickRow> rows;
    for (const PhysicalDevice& device : physicalDevices()) {
        const auto picked = std::ranges::find(current, device.id, &BackendPick::device);
        const std::optional<BackendKind> pickedKind =
            picked == current.end() ? std::nullopt : std::optional<BackendKind>(picked->kind);
        const bool lastOn = pickedKind && current.size() == 1;
        DevicePickRow row{device.id, physicalDeviceLabel(device)};
        row.options.push_back({"Off", !lastOn, !pickedKind});
        row.kinds.push_back(std::nullopt);
        for (const BackendKind kind : pickableKinds(device)) {
            const DeviceBackendOption* option = device.option(kind);
            const bool available = option && deviceBackendStatus(kind, option->pluginDevice).available;
            row.options.push_back({std::string(backendKindName(kind)), available, pickedKind == kind});
            row.kinds.push_back(kind);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::optional<std::vector<BackendPick>> pickDeviceOption(std::span<const BackendPick> current,
                                                         std::span<const DevicePickRow> rows, size_t row,
                                                         size_t option) {
    if (row >= rows.size() || option >= rows[row].options.size()) {
        return std::nullopt;
    }
    const UiOption& clicked = rows[row].options[option];
    if (!clicked.enabled || clicked.selected) {
        return std::nullopt;
    }
    return withDevicePick(current, rows[row].device, rows[row].kinds[option]);
}

UiOption releaseButton(const SessionView& view) {
    if (view.state == SessionState::Ready) {
        return {"Start (1 click)", true};
    }
    return {std::format("Auto releasing - {} launchers left", view.launchers.launcherCount()), false};
}

std::vector<UiOption> spawnRateChoices(int selectedIndex) {
    std::vector<UiOption> options;
    for (size_t index = 0; index < ::spawnRateOptions.size(); ++index) {
        const int rate = ::spawnRateOptions[index];
        options.push_back({rate == 0 ? std::string("Max") : spawnRateOptionLabel(rate), true,
                           static_cast<int>(index) == selectedIndex});
    }
    return options;
}

std::string waitingToSpawnLabel(const SessionView& view) {
    return view.endlessSpawn ? std::string("Endless") : groupedCount(view.pendingSpawn);
}

bool isAutoBallLimit(const SessionView& view) { return view.usingMachineLimit; }

std::vector<UiOption> ballLimitChoices(const SessionView& view) {
    constexpr std::array<std::string_view, ballLimitChoiceCount> labels{"Level", "Auto", "Max 2^64-1"};
    std::vector<UiOption> options;
    for (int index = 0; index < ballLimitChoiceCount; ++index) {
        options.push_back({std::string(labels[static_cast<size_t>(index)]), true, index == view.ballLimitIndex});
    }
    return options;
}

std::vector<BackendRow> gameplayBackendRows(const SessionView& view) {
    std::vector<BackendRow> rows;
    for (const BackendKind kind : selectableBackends()) {
        const BackendStatus status = backendStatus(kind);
        const bool starting = view.pendingBackend == kind;
        BackendRow row{kind, {std::string(backendKindName(kind)), status.available}};
        if (!status.available) {
            row.option.detail = status.unavailableReason;
        } else if (starting) {
            row.option.label += " - starting...";
        }
        row.option.selected = starting || (view.backend == kind && !view.pendingBackend);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool ballCollisionsOffered() { return isBackendSelectable(BackendKind::Scalar); }

bool backendChoiceOffered() { return selectableBackends().size() > 1; }

BallCollisionsChoice ballCollisionsChoice(bool requested) {
    const bool possible = isBackendSelectable(BackendKind::Scalar);
    BallCollisionsChoice choice{{{"Off", true, !requested}, {"On", possible, requested}}};
    choice.hint = "Testing only - the contest rules have balls pass through each other.";
    if (!possible) {
        choice.hint += " Needs Scalar, not offered here.";
    }
    return choice;
}

std::vector<DebugRow> debugGadgetRows(const SessionView& view, const FrameTimings& frames,
                                      const DebugGadgetOptions& options) {
    DebugRows rows;
    const SimulationStats& stats = view.stats;
    rows.heading("Performance");
    const double fps = frames.framesPerSecond();
    const double minimumFps = frames.worstFpsLastSecond();
    rows.value("FPS", std::format("{:.0f}", fps), fpsTone(fps));
    rows.value("Min FPS (1s)", std::format("{:.0f}", minimumFps), fpsTone(minimumFps));
    rows.value("Frame ms", std::format("{:.2f}", frames.average().frameMs));
    rows.value("Physics ms", std::format("{:.2f}", stats.lastFramePhysicsMs), physicsTone(view));
    rows.value("GPU ms", std::format("{:.2f}", frames.average().gpuMs));
    for (const auto& [label, value] : options.portRows) {
        rows.value(label, value);
    }
    if (options.showBackend) {
        rows.value("Backend", std::string(backendKindName(view.backend)));
        if (!options.renderer.empty()) {
            rows.value("Renderer", options.renderer);
        }
        if (const std::string detail = backendStatus(view.backend).detail; !detail.empty()) {
            rows.value("Device", detail);
        }
        if (view.deviceStepMs > 0.0) {
            rows.value("Physics kernel", std::format("{:.2f} ms", view.deviceStepMs));
        }
        if (const std::optional<HybridSplit>& hybrid = view.hybrid) {
            rows.value("CPU share", std::format("{:.1f}%", 100.0 * static_cast<double>(hybrid->cpuBalls) /
                                                              static_cast<double>(std::max<BallCount>(
                                                                  hybrid->totalBalls, 1))));
            rows.value("CPU / GPU step", std::format("{:.2f} / {:.2f} ms", hybrid->cpuStepMs, hybrid->gpuStepMs));
        }
        if (ballCollisionsOffered()) {
            const bool unsupported = view.collisionsRequested && !view.collisionsActive;
            rows.value("Ball collisions",
                       view.collisionsRequested ? (view.collisionsActive ? "On" : "On (unsupported)") : "Off",
                       unsupported ? UiTone::Danger : UiTone::Normal);
        }
    }

    rows.heading("Objects");
    const BallCount balls = stats.ballCount;
    const BallCount bricks = view.board ? static_cast<BallCount>(view.board->aliveBrickCount()) : 0;
    const BallCount launchers = static_cast<BallCount>(view.launchers.launcherCount());
    rows.value("Total objects", groupedCount(balls + bricks + launchers));
    rows.value("Balls", groupedCount(balls));
    rows.value("Bricks", groupedCount(bricks));
    rows.value("Launchers", groupedCount(launchers));
    rows.value("Waiting to spawn", waitingToSpawnLabel(view));
    if (view.mode == GameMode::Performance) {
        std::string limit;
        if (view.ballLimitIndex == static_cast<int>(BallLimitChoice::Level)) {
            limit = "Level";
        } else if (view.usingMachineLimit) {
            limit = view.machineLimit ? "Auto " + groupedCount(*view.machineLimit) : std::string("Auto");
        } else {
            limit = view.ballLimit == unlimitedBalls ? std::string("Max (2^64-1)") : groupedCount(view.ballLimit);
        }
        rows.value("Ball limit", std::move(limit));
    }

    rows.heading("Balls per color");
    for (int color = 0; color < ballColorCount; ++color) {
        rows.color(color, groupedCount(stats.ballsByColor[static_cast<size_t>(color)]));
    }
    if (!view.backendFault.empty()) {
        rows.heading("Backend fault");
        rows.value("Fell back to CPU", view.backendFault, UiTone::Danger);
    }
    return std::move(rows.rows);
}

std::string backendFailedMessage(BackendKind kind) {
    return std::format("Could not start the {} backend", backendKindName(kind));
}

std::string physicsOnMessage(std::span<const BackendPick> picks) { return "Physics on " + backendPicksLabel(picks); }

std::string computeGpuMessage(std::string_view gpuName) { return std::format("Vulkan physics on {}", gpuName); }
