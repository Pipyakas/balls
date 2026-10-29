#include "screens/SettingsPanel.h"

#include "render/Palette.h"
#include "screens/DevicePicker.h"
#include "sim/BackendFactory.h"
#include "sim/BackendWorker.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <format>

namespace {

constexpr float panelPadding = 24.0f;
constexpr float titleSize = 40.0f;
constexpr float titleHeight = 56.0f;
constexpr float sectionSize = 20.0f;
constexpr float sectionHeight = 30.0f;
constexpr float optionHeight = 48.0f;
constexpr float optionGap = 10.0f;
constexpr float hintSize = 18.0f;
constexpr float hintHeight = 24.0f;
constexpr float hintGap = 8.0f;
constexpr float blockGap = 16.0f;
constexpr float backHeight = 56.0f;
constexpr float scoreInset = 16.0f;
constexpr float runBenchmarkWidth = 240.0f;
constexpr double ballStepsPerMillion = 1e6;
constexpr DevicePickerStyle devicePickerStyle{optionHeight, optionGap, 0.34f, hintSize};

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
        return found->estimatedBalls > 0.0
                   ? std::format("{} balls", shortBallCount(found->estimatedBalls))
                   : std::format("{:.0f} M/s", found->ballStepsPerSecond / ballStepsPerMillion);
    case CandidateStatus::Failed:
        return "failed";
    default:
        return {};
    }
}

constexpr std::array<std::pair<LayoutOrientation, const char*>, 3> layoutOptions{{
    {LayoutOrientation::Auto, "Auto"},
    {LayoutOrientation::Landscape, "Landscape"},
    {LayoutOrientation::Portrait, "Portrait"},
}};

struct ApiChoice {
    const char* id;
    const char* label;
};

#if defined(_WIN32)
constexpr std::array<ApiChoice, 3> apiChoices{{
    {"auto", "Auto"},
    {"d3d12", "Direct3D 12"},
    {"vulkan", "Vulkan"},
}};
#else
constexpr std::array<ApiChoice, 2> apiChoices{{
    {"auto", "Auto"},
    {"vulkan", "Vulkan"},
}};
#endif

// One section heading, its option row and one hint line.
constexpr float blockHeight = sectionHeight + optionHeight + hintGap + hintHeight + blockGap;

} // namespace

float settingsContentHeight() {
    float height = panelPadding * 2.0f;
    height += titleHeight + blockGap;
    height += blockHeight;
    height += sectionHeight + blockHeight;
    height += blockHeight;
    height += blockHeight;
    height += sectionHeight + sectionHeight + (optionHeight + optionGap) * selectableBackends().size() -
              optionGap + blockGap - optionGap;
    height += sectionHeight + devicePickerHeight(devicePickerStyle) + hintGap + hintHeight * 2.0f + blockGap;
    height += backHeight;
    return height;
}

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend,
                                    std::span<const CandidateResult> benchmarkScores, bool webGpuSupported,
                                    float& scroll, bool canRunBenchmark) {
    SettingsPanelEdit edit{current, false, false, std::nullopt};
    const Rect inner{area.x, area.y, area.width, area.height};
    float visibleHeight = 0.0f;
    ui::scrollFrame(inner, scroll, settingsContentHeight(), visibleHeight);
    gfx::beginClip(inner);
    const float innerX = area.x + panelPadding;
    const float innerWidth = area.width - panelPadding * 2.0f;
    float cursor = area.y + panelPadding - scroll;

    ui::drawTextInRect("Settings", {innerX, cursor, innerWidth, titleHeight}, titleSize, palette::text);
    cursor += titleHeight + blockGap;

    ui::drawText("Layout", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    const Rect layoutButtons{innerX, cursor, innerWidth, optionHeight};
    for (size_t i = 0; i < layoutOptions.size(); ++i) {
        const auto& [orientation, label] = layoutOptions[i];
        if (ui::button(ui::columnSlice(layoutButtons, static_cast<int>(i),
                                       static_cast<int>(layoutOptions.size()), optionGap),
                       label, true, current.layout == orientation) &&
            current.layout != orientation) {
            edit.settings.layout = orientation;
            edit.changed = true;
        }
    }
    cursor += optionHeight + hintGap;
    ui::drawText("Auto follows the window shape.", {innerX, cursor}, hintSize, palette::textMuted);
    cursor += hintHeight + blockGap;

    ui::drawText("Display", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    ui::drawText("VSync", {innerX, cursor}, sectionSize, palette::text);
    cursor += sectionHeight;
    const Rect vsyncOptions{innerX, cursor, innerWidth, optionHeight};
    if (ui::button(ui::columnSlice(vsyncOptions, 0, 2, optionGap), "Off", true, !current.vsync) &&
        current.vsync) {
        edit.settings.vsync = false;
        edit.changed = true;
    }
    if (ui::button(ui::columnSlice(vsyncOptions, 1, 2, optionGap), "On", true, current.vsync) &&
        !current.vsync) {
        edit.settings.vsync = true;
        edit.changed = true;
    }
    cursor += optionHeight + hintGap;
    ui::drawText("Replaces the old V shortcut.", {innerX, cursor}, hintSize, palette::textMuted);
    cursor += hintHeight + blockGap;

    ui::drawText("Renderer", {innerX, cursor}, sectionSize, palette::text);
    cursor += sectionHeight;
    const Rect rendererOptions{innerX, cursor, innerWidth, optionHeight};
    const bool isNvrhi = current.renderer == "nvrhi";
    const bool isWebGpu = current.renderer == "webgpu";
    if (ui::button(ui::columnSlice(rendererOptions, 0, 2, optionGap), "NVRHI", true, isNvrhi) && !isNvrhi) {
        edit.settings.renderer = "nvrhi";
        edit.changed = true;
    }
    const std::string webGpuLabel = webGpuSupported ? "WebGPU" : "WebGPU - not built";
    if (ui::button(ui::columnSlice(rendererOptions, 1, 2, optionGap), webGpuLabel.c_str(), webGpuSupported,
                   isWebGpu) &&
        webGpuSupported && !isWebGpu) {
        edit.settings.renderer = "webgpu";
        edit.changed = true;
    }
    cursor += optionHeight + hintGap;
    ui::drawText("Desktop only. Both draw the same frame.", {innerX, cursor}, hintSize, palette::textMuted);
    cursor += hintHeight + blockGap;

    ui::drawText("Graphics API", {innerX, cursor}, sectionSize, palette::text);
    cursor += sectionHeight;
    const Rect apiButtons{innerX, cursor, innerWidth, optionHeight};
    for (size_t i = 0; i < apiChoices.size(); ++i) {
        const ApiChoice& api = apiChoices[i];
        const bool selected = current.graphicsApi == api.id;
        if (ui::button(ui::columnSlice(apiButtons, static_cast<int>(i),
                                       static_cast<int>(apiChoices.size()), optionGap),
                       api.label, true, selected) &&
            !selected) {
            edit.settings.graphicsApi = api.id;
            edit.changed = true;
        }
    }
    cursor += optionHeight + hintGap;
    ui::drawText("The native API under the selected renderer.", {innerX, cursor}, hintSize,
                 palette::textMuted);
    cursor += hintHeight + blockGap;

    ui::drawText("Physics", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    ui::drawText("Default backend", {innerX, cursor}, sectionSize, palette::text);
    if (canRunBenchmark) {
        const Rect runBenchmark{innerX + innerWidth - runBenchmarkWidth, cursor - optionGap, runBenchmarkWidth,
                                sectionHeight + optionGap * 0.5f};
        if (ui::button(runBenchmark, "Run machine benchmark")) {
            edit.runBenchmark = true;
        }
    }
    cursor += sectionHeight;
    for (const BackendKind kind : selectableBackends()) {
        const BackendStatus status = backendStatus(kind);
        const bool available = status.available;
        std::string label(backendKindName(kind));
        if (!available) {
            label += std::string(" - ") + status.unavailableReason;
        }
        const Rect row{innerX, cursor, innerWidth, optionHeight};
        if (ui::button(row, label, available, selectedBackend == kind) && available) {
            edit.backend = kind;
            edit.changed = true;
        }
        if (const std::string score = benchmarkScore(benchmarkScores, kind); !score.empty()) {
            const bool selected = selectedBackend == kind;
            ui::drawTextInRect(score, {row.x, row.y, row.width - scoreInset, row.height}, hintSize,
                               selected ? palette::panel : palette::textMuted, ui::Align::Right);
        }
        cursor += optionHeight + optionGap;
    }
    cursor += blockGap - optionGap;

    ui::drawText("Devices", {innerX, cursor}, sectionSize, palette::text);
    cursor += sectionHeight;
    const std::vector<BackendPick> picks = displayedPicks(selectedBackend);
    if (const auto changed = drawDevicePicker({innerX, cursor, innerWidth, devicePickerHeight(devicePickerStyle)},
                                              picks, devicePickerStyle)) {
        setBackendPicks(*changed);
        refreshBackendStatuses();
        edit.backend = kindForPicks(*changed);
        edit.changed = true;
    }
    cursor += devicePickerHeight(devicePickerStyle) + hintGap;
    ui::drawText("One backend per device. Two or more devices run as Hybrid,", {innerX, cursor}, hintSize,
                 palette::textMuted);
    cursor += hintHeight;
    ui::drawText("splitting the balls between them; one runs that backend alone.", {innerX, cursor}, hintSize,
                 palette::textMuted);
    cursor += hintHeight + blockGap;

    if (ui::button({innerX, cursor, innerWidth, backHeight}, "Back")) {
        edit.backPressed = true;
    }
    // Kept clear of the panel's rounded corners, which the fade would square off.
    ui::scrollHints({innerX, inner.y, innerWidth, inner.height}, scroll, settingsContentHeight(), palette::panel);
    gfx::endClip();
    return edit;
}
