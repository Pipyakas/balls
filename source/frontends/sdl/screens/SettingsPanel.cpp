#include "screens/SettingsPanel.h"

#include "render/Palette.h"
#include "sim/BackendFactory.h"
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

std::string benchmarkScore(std::span<const CandidateResult> scores, BackendKind kind) {
    const auto found = std::ranges::find_if(scores, [&](const CandidateResult& result) {
        return result.candidate.kind == kind;
    });
    if (found == scores.end()) {
        return {};
    }
    switch (found->status) {
    case CandidateStatus::Done:
        return std::format("{:.0f} M/s", found->ballStepsPerSecond / ballStepsPerMillion);
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

} // namespace

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend,
                                    std::span<const CandidateResult> benchmarkScores) {
    SettingsPanelEdit edit{current, false, false, std::nullopt};
    const float innerX = area.x + panelPadding;
    const float innerWidth = area.width - panelPadding * 2.0f;
    float cursor = area.y + panelPadding;

    ui::drawTextInRect("Settings", {innerX, cursor, innerWidth, titleHeight}, titleSize, palette::text);
    cursor += titleHeight + blockGap;

    ui::drawText("Gameplay", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    ui::drawText("Ball collisions", {innerX, cursor}, sectionSize, palette::text);
    cursor += sectionHeight;
    const Rect collisionOptions{innerX, cursor, innerWidth, optionHeight};
    if (ui::button(ui::columnSlice(collisionOptions, 0, 2, optionGap), "Off", true,
                   !current.ballCollisions) &&
        current.ballCollisions) {
        edit.settings.ballCollisions = false;
        edit.changed = true;
    }
    if (ui::button(ui::columnSlice(collisionOptions, 1, 2, optionGap), "On", true, current.ballCollisions) &&
        !current.ballCollisions) {
        edit.settings.ballCollisions = true;
        edit.changed = true;
    }
    cursor += optionHeight + hintGap;
    ui::drawText("Testing only - the game rules have balls", {innerX, cursor}, hintSize,
                 palette::textMuted);
    cursor += hintHeight;
    ui::drawText("pass through each other.", {innerX, cursor}, hintSize, palette::textMuted);
    cursor += hintHeight + blockGap;

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

    ui::drawText("Physics", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    ui::drawText("Default backend", {innerX, cursor}, sectionSize, palette::text);
    const Rect runBenchmark{innerX + innerWidth - runBenchmarkWidth, cursor - optionGap, runBenchmarkWidth,
                            sectionHeight + optionGap * 0.5f};
    if (ui::button(runBenchmark, "Run machine benchmark")) {
        edit.runBenchmark = true;
    }
    cursor += sectionHeight;
    for (const BackendKind kind : allBackendKinds) {
        const bool available = isBackendAvailable(kind);
        std::string label(backendKindName(kind));
        if (!available) {
            label += std::string(" - ") + backendUnavailableReason(kind);
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

    if (ui::button({innerX, cursor, innerWidth, backHeight}, "Back")) {
        edit.backPressed = true;
    }
    return edit;
}
