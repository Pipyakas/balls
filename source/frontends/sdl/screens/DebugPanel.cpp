#include "screens/DebugPanel.h"

#include "render/Palette.h"
#include "ui/MenuModel.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <format>
#include <string>

namespace {

constexpr float lineHeight = 26.0f;
constexpr float valueSize = 20.0f;
constexpr float labelSize = 16.0f;
constexpr float labelHeight = 22.0f;
constexpr float headingSize = 24.0f;
constexpr float padding = 16.0f;
constexpr float swatchSize = 14.0f;
constexpr float textSize = 20.0f;

class PanelWriter {
public:
    PanelWriter(Rect area) : area(area), cursor(area.y + padding) {}

    void heading(std::string_view text) {
        cursor += 6.0f;
        ui::drawText(text, {area.x + padding, cursor}, headingSize, palette::accent);
        cursor += lineHeight + 4.0f;
    }

    void row(std::string_view label, std::string_view value, Color valueColor = palette::text) {
        ui::drawText(label, {area.x + padding, cursor}, labelSize, palette::textMuted);
        cursor += labelHeight;
        ui::drawText(value, {area.x + padding, cursor}, valueSize, valueColor);
        cursor += lineHeight;
    }

    void colorRow(int colorIndex, std::string_view value) {
        const float swatchY = cursor + (textSize - swatchSize) * 0.5f;
        gfx::fillCircle({area.x + padding + swatchSize * 0.5f, swatchY + swatchSize * 0.5f},
                        swatchSize * 0.5f, palette::ballColor(colorIndex));
        ui::drawText(ballColorName(ballColorFromIndex(colorIndex)),
                     {area.x + padding + swatchSize + 8.0f, cursor}, textSize, palette::textMuted);
        ui::drawText(value, {area.x + area.width - padding, cursor}, textSize, palette::text,
                     ui::Align::Right);
        cursor += lineHeight;
    }

    // How tall everything written so far is, padding included: what the gadget scrolls over.
    float contentHeight() const { return cursor - area.y + padding; }

private:
    Rect area;
    float cursor;
};

Color fpsColor(float fps) {
    constexpr float targetFps = 60.0f;
    return fps >= targetFps ? palette::success : palette::danger;
}

} // namespace

void drawDebugPanel(Rect area, const SessionView& session, const FrameTimings& frames, DebugPanelState& state,
                    bool showBackend, std::string_view deviceDescription, std::string_view renderer) {
    ui::drawPanel(area, palette::panel, 0.03f);
    const FrameSample& sample = frames.average();
    DebugGadgetOptions options;
    options.showBackend = showBackend;
    options.renderer = renderer.empty() ? std::string(deviceDescription) : std::string(renderer);
    // SDL's own rows: the work rate without the present wait, the rate frames reach the screen, and the CPU and
    // present split of the frame.
    options.portRows = {{"Work FPS", std::format("{:.0f}", frames.workFramesPerSecond())},
                        {"Min work FPS", std::format("{:.0f}", frames.worstWorkFpsLastSecond())},
                        {"Shown FPS", std::format("{:.0f}", frames.shownFramesPerSecond())},
                        {"CPU", std::format("{:.2f} ms", sample.cpuMs)},
                        {"Present", std::format("{:.2f} ms", sample.presentMs)}};
    const std::vector<DebugRow> rows = debugGadgetRows(session, frames, options);
    const float contentHeight = std::max(state.contentHeight, area.height);
    float visibleHeight = 0.0f;
    ui::scrollFrame(area, state.scroll, contentHeight, visibleHeight);
    const Rect inner{area.x, area.y - state.scroll, area.width, contentHeight};
    PanelWriter writer(inner);
    gfx::beginClip(area);
    for (const DebugRow& row : rows) {
        switch (row.kind) {
        case DebugRow::Kind::Heading:
            writer.heading(row.label);
            break;
        case DebugRow::Kind::Value: {
            Color color = palette::text;
            switch (row.tone) {
            case UiTone::Muted:
                color = palette::textMuted;
                break;
            case UiTone::Accent:
                color = palette::accent;
                break;
            case UiTone::Success:
                color = palette::success;
                break;
            case UiTone::Danger:
                color = palette::danger;
                break;
            default:
                break;
            }
            writer.row(row.label, row.value, color);
            break;
        }
        case DebugRow::Kind::Color:
            writer.colorRow(row.colorIndex, row.value);
            break;
        }
    }
    state.contentHeight = writer.contentHeight();
    ui::scrollHints(area, state.scroll, contentHeight, palette::panel);
    gfx::endClip();
}

void drawPortraitDebugStrip(Rect area, const SessionView& session, const FrameTimings& frames, bool showBackend) {
    constexpr float stripLabelSize = 15.0f;
    constexpr float stripValueSize = 18.0f;
    constexpr float horizontalPadding = 12.0f;
    constexpr float itemGap = 16.0f;
    const FrameSample& sample = frames.average();
    const float fps = static_cast<float>(frames.framesPerSecond());
    const float minimumFps = static_cast<float>(frames.worstFpsLastSecond());
    constexpr int itemCount = 8;
    const std::string values[itemCount] = {
        std::format("{:.0f}", fps),
        std::format("{:.0f}", minimumFps),
        std::format("{:.2f}", sample.frameMs),
        std::format("{:.2f}", sample.gpuMs),
        groupedCount(session.stats.ballCount),
        groupedCount(static_cast<BallCount>(session.board->aliveBrickCount())),
        groupedCount(session.launchers.launcherCount()),
        std::string(backendKindName(session.backend)),
    };
    constexpr const char* labels[itemCount] = {"FPS",   "Min",    "Frame",  "GPU",
                                               "Balls", "Bricks", "Launch", "Backend"};
    const int shownItems = showBackend ? itemCount : itemCount - 1;
    float widths[itemCount] = {};
    for (int index = 0; index < itemCount; ++index) {
        widths[index] = std::max(ui::measureText(labels[index], stripLabelSize).x,
                                 ui::measureText(values[index], stripValueSize).x);
    }
    int visibleCount = shownItems;
    while (visibleCount > 0) {
        float needed = 0.0f;
        for (int index = 0; index < visibleCount; ++index) {
            needed += widths[index];
        }
        needed += itemGap * static_cast<float>(visibleCount - 1);
        if (needed <= area.width - horizontalPadding * 2.0f) {
            break;
        }
        --visibleCount;
    }
    if (visibleCount == 0) {
        return;
    }
    float used = 0.0f;
    for (int index = 0; index < visibleCount; ++index) {
        used += widths[index];
    }
    used += itemGap * static_cast<float>(visibleCount - 1);
    float gap = itemGap;
    if (visibleCount > 1) {
        gap += (area.width - horizontalPadding * 2.0f - used) / static_cast<float>(visibleCount - 1);
    }
    float itemX = visibleCount > 1 ? area.x + horizontalPadding : area.x + (area.width - widths[0]) * 0.5f;
    const float centerY = area.y + area.height * 0.5f;
    gfx::beginClip(area);
    for (int index = 0; index < visibleCount; ++index) {
        ui::drawText(labels[index], {itemX, centerY - stripLabelSize - 2.0f}, stripLabelSize, palette::textMuted);
        const Color color = index == 0 ? fpsColor(fps) : (index == 1 ? fpsColor(minimumFps) : palette::text);
        ui::drawText(values[index], {itemX, centerY + 2.0f}, stripValueSize, color);
        ui::drawText(values[index], {itemX + 1.0f, centerY + 2.0f}, stripValueSize, color);
        itemX += widths[index] + gap;
    }
    gfx::endClip();
}
