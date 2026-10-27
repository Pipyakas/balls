#include "screens/SettingsPanel.h"

#include "render/Palette.h"
#include "sim/BackendFactory.h"
#include "ui/Widgets.h"

#include <array>

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

} // namespace

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend, bool webGpuSupported) {
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
    if (ui::button(ui::columnSlice(rendererOptions, 1, 2, optionGap), webGpuLabel.c_str(), webGpuSupported, isWebGpu) &&
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
        if (ui::button(ui::columnSlice(apiButtons, static_cast<int>(i), static_cast<int>(apiChoices.size()), optionGap),
                       api.label, true, selected) &&
            !selected) {
            edit.settings.graphicsApi = api.id;
            edit.changed = true;
        }
    }
    cursor += optionHeight + hintGap;
    ui::drawText("The native API under the selected renderer.", {innerX, cursor}, hintSize, palette::textMuted);
    cursor += hintHeight + blockGap;

    ui::drawText("Physics", {innerX, cursor}, sectionSize, palette::textMuted);
    cursor += sectionHeight;
    ui::drawText("Default backend", {innerX, cursor}, sectionSize, palette::text);
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
        cursor += optionHeight + optionGap;
    }
    cursor += blockGap - optionGap;

    if (ui::button({innerX, cursor, innerWidth, backHeight}, "Back")) {
        edit.backPressed = true;
    }
    return edit;
}
