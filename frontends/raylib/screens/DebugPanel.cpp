#include "screens/DebugPanel.h"

#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <format>
#include <string>

namespace {

constexpr float lineHeight = 26.0f;
constexpr float textSize = 20.0f;
constexpr float headingSize = 24.0f;
constexpr float padding = 16.0f;
constexpr float swatchSize = 14.0f;

class PanelWriter {
public:
    PanelWriter(Rectangle area) : area(area), cursor(area.y + padding) {}

    void heading(std::string_view text) {
        cursor += 6.0f;
        ui::drawText(text, {area.x + padding, cursor}, headingSize, palette::accent);
        cursor += lineHeight + 4.0f;
    }

    void row(std::string_view label, std::string_view value, Color valueColor = palette::text) {
        ui::drawText(label, {area.x + padding, cursor}, textSize, palette::textMuted);
        ui::drawText(value, {area.x + area.width - padding, cursor}, textSize, valueColor, ui::Align::Right);
        cursor += lineHeight;
    }

    void colorRow(int colorIndex, std::string_view value) {
        const float swatchY = cursor + (textSize - swatchSize) * 0.5f;
        DrawCircleV({area.x + padding + swatchSize * 0.5f, swatchY + swatchSize * 0.5f}, swatchSize * 0.5f,
                    palette::ballColor(colorIndex));
        ui::drawText(ballColorName(ballColorFromIndex(colorIndex)), {area.x + padding + swatchSize + 8.0f, cursor},
                     textSize, palette::textMuted);
        ui::drawText(value, {area.x + area.width - padding, cursor}, textSize, palette::text, ui::Align::Right);
        cursor += lineHeight;
    }

private:
    Rectangle area;
    float cursor;
};

std::string withThousands(long long value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (int insertAt = static_cast<int>(digits.size()) - 3; insertAt > 0; insertAt -= 3) {
        digits.insert(static_cast<size_t>(insertAt), ",");
    }
    return value < 0 ? "-" + digits : digits;
}

Color fpsColor(float fps) {
    constexpr float targetFps = 60.0f;
    return fps >= targetFps ? palette::success : palette::danger;
}

}

void drawDebugPanel(Rectangle area, const GameSession& session, const FrameTimings& frames) {
    ui::drawPanel(area, palette::panel, 0.03f);
    PanelWriter writer(area);
    const Simulation& simulation = session.simulation();
    const SimulationStats& stats = simulation.stats();

    writer.heading("Performance");
    const FrameSample& sample = frames.average();
    const float fps = static_cast<float>(frames.framesPerSecond());
    const float minimumFps = static_cast<float>(frames.worstFpsLastSecond());
    writer.row("FPS", std::format("{:.0f}", fps), fpsColor(fps));
    writer.row("Min FPS (1s)", std::format("{:.0f}", minimumFps), fpsColor(minimumFps));
    writer.row("Frame", std::format("{:.2f} ms", sample.frameMs));
    writer.row("CPU", std::format("{:.2f} ms", sample.cpuMs));
    writer.row("  Physics", std::format("{:.2f} ms  x{}", stats.lastFramePhysicsMs, stats.ticksLastFrame));
    writer.row("GPU", std::format("{:.2f} ms", sample.gpuMs));
    writer.row("Present", std::format("{:.2f} ms", sample.presentMs));
    writer.row("Backend", backendKindName(session.backend()));
    if (const std::string detail = backendDetail(session.backend()); !detail.empty()) {
        writer.row("Device", detail);
    }

    writer.heading("Objects");
    const long long balls = static_cast<long long>(stats.ballCount);
    const long long bricks = simulation.board().aliveBrickCount();
    const long long launchers = session.launchers().launcherCount();
    writer.row("Total objects", withThousands(balls + bricks + launchers));
    writer.row("Balls", withThousands(balls));
    writer.row("Bricks", withThousands(bricks));
    writer.row("Launchers", withThousands(launchers));
    writer.row("Waiting to spawn", withThousands(session.spawner().pendingBalls()));

    writer.heading("Balls per color");
    for (int color = 0; color < ballColorCount; ++color) {
        writer.colorRow(color, withThousands(stats.ballsByColor[color]));
    }
    if (!session.lastBackendFault().empty()) {
        writer.heading("Backend fault");
        writer.row("Fell back to CPU", "", palette::danger);
    }
}
