#include "screens/DebugPanel.h"

#include "render/Palette.h"
#include "sim/HybridBackend.h"
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

private:
    Rect area;
    float cursor;
};

std::string withThousands(BallCount value) {
    std::string digits = std::to_string(value);
    for (int insertAt = static_cast<int>(digits.size()) - 3; insertAt > 0; insertAt -= 3) {
        digits.insert(static_cast<size_t>(insertAt), ",");
    }
    return digits;
}

std::string ballLimitText(BallCount limit) {
    return limit == unlimitedBalls ? std::string("Max (2^64-1)") : withThousands(limit);
}

Color fpsColor(float fps) {
    constexpr float targetFps = 60.0f;
    return fps >= targetFps ? palette::success : palette::danger;
}

int debugRowCount(const Simulation& simulation) {
    int rows = 9;
    if (simulation.backend().deviceStepMs() > 0.0) {
        rows += 1;
    }
    if (dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        rows += 2;
    }
    rows += 5;
    return rows;
}

} // namespace

void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames,
                    DebugPanelState& state) {
    ui::drawPanel(area, palette::panel, 0.03f);
    const float contentHeight = debugContentHeight(session.simulation());
    float visibleHeight = 0.0f;
    ui::scrollFrame(area, state.scroll, contentHeight, visibleHeight);
    const Rect inner{area.x, area.y - state.scroll, area.width, contentHeight};
    PanelWriter writer(inner);
    gfx::beginClip(area);
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
    if (const double kernelMs = simulation.backend().deviceStepMs(); kernelMs > 0.0) {
        writer.row("  Physics kernel", std::format("{:.2f} ms", kernelMs));
    }
    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        writer.row("  CPU share",
                   std::format("{:.1f}%", 100.0 * static_cast<double>(hybrid->cpuBallCount()) /
                                              static_cast<double>(std::max<size_t>(hybrid->ballCount(), 1))));
        writer.row("  CPU / GPU step",
                   std::format("{:.2f} / {:.2f} ms", hybrid->cpuStepMs(), hybrid->gpuStepMs()));
    }
    writer.row("Present", std::format("{:.2f} ms", sample.presentMs));
    const CapacityProbe& probe = session.capacityProbe();
    if (probe.phase() != ProbePhase::Idle) {
        writer.row("Probe", std::format("{} {:.0f}/{:.0f} fps", probePhaseName(probe.phase()),
                                        probe.lastFps(), probe.targetFps()));
    }
    writer.row("Backend", backendKindName(session.backend()));
    if (const std::string detail = backendDetail(session.backend()); !detail.empty()) {
        writer.row("Device", detail);
    }
    const bool collisionsWanted = session.simulation().ballCollisionsRequested();
    const bool collisionsOn = session.simulation().ballCollisionsActive();
    writer.row("Ball collisions", collisionsWanted ? (collisionsOn ? "On" : "On (unsupported)") : "Off",
               collisionsWanted && !collisionsOn ? palette::danger : palette::text);

    writer.heading("Objects");
    const BallCount balls = stats.ballCount;
    const BallCount bricks = static_cast<BallCount>(simulation.board().aliveBrickCount());
    const BallCount launchers = static_cast<BallCount>(session.launchers().launcherCount());
    writer.row("Total objects", withThousands(balls + bricks + launchers));
    writer.row("Balls", withThousands(balls));
    writer.row("Bricks", withThousands(bricks));
    writer.row("Launchers", withThousands(launchers));
    writer.row("Waiting to spawn", withThousands(session.spawner().pendingBalls()));
    if (session.mode() == GameMode::Performance) {
        const std::string resolvedLimit =
            session.machineBallLimit() ? std::format("Auto {}", withThousands(*session.machineBallLimit()))
                                       : std::string("Auto (probe needed)");
        writer.row("Ball limit",
                   session.isUsingMachineBallLimit() ? resolvedLimit : ballLimitText(session.ballLimit()));
    }

    writer.heading("Balls per color");
    for (int color = 0; color < ballColorCount; ++color) {
        writer.colorRow(color, withThousands(stats.ballsByColor[color]));
    }
    if (!session.lastBackendFault().empty()) {
        writer.heading("Backend fault");
        writer.row("Fell back to CPU", "", palette::danger);
    }
    gfx::endClip();
}

void drawPortraitDebugStrip(Rect area, const GameSession& session, const FrameTimings& frames) {
    constexpr float labelSize = 15.0f;
    constexpr float valueSize = 18.0f;
    constexpr float horizontalPadding = 12.0f;
    constexpr float itemGap = 16.0f;
    const Simulation& simulation = session.simulation();
    const FrameSample& sample = frames.average();
    const float fps = static_cast<float>(frames.framesPerSecond());
    const float minimumFps = static_cast<float>(frames.worstFpsLastSecond());
    constexpr int itemCount = 8;
    const std::string values[itemCount] = {
        std::format("{:.0f}", fps),
        std::format("{:.0f}", minimumFps),
        std::format("{:.2f}", sample.frameMs),
        std::format("{:.2f}", sample.gpuMs),
        withThousands(simulation.stats().ballCount),
        withThousands(static_cast<BallCount>(simulation.board().aliveBrickCount())),
        withThousands(session.launchers().launcherCount()),
        std::string(backendKindName(session.backend())),
    };
    constexpr const char* labels[itemCount] = {"FPS",   "Min",    "Frame",  "GPU",
                                               "Balls", "Bricks", "Launch", "Backend"};
    float widest = 0.0f;
    for (int index = 0; index < itemCount; ++index) {
        widest = std::max(widest, ui::measureText(labels[index], labelSize).x);
        widest = std::max(widest, ui::measureText(values[index], valueSize).x);
    }
    int visibleCount = itemCount;
    while (visibleCount > 0 &&
           widest * static_cast<float>(visibleCount) + itemGap * static_cast<float>(visibleCount - 1) >
               area.width - horizontalPadding * 2.0f) {
        --visibleCount;
    }
    if (visibleCount == 0) {
        return;
    }
    float gap = itemGap;
    float startX = area.x + horizontalPadding;
    if (visibleCount > 1) {
        gap += (area.width - horizontalPadding * 2.0f - widest * static_cast<float>(visibleCount) -
                itemGap * static_cast<float>(visibleCount - 1)) /
               static_cast<float>(visibleCount - 1);
    } else {
        startX = area.x + (area.width - widest) * 0.5f;
    }
    const float centerY = area.y + area.height * 0.5f;
    gfx::beginClip(area);
    for (int index = 0; index < visibleCount; ++index) {
        const float itemX = startX + static_cast<float>(index) * (widest + gap);
        ui::drawText(labels[index], {itemX, centerY - labelSize - 2.0f}, labelSize, palette::textMuted);
        const Color color = index == 0 ? fpsColor(fps) : (index == 1 ? fpsColor(minimumFps) : palette::text);
        ui::drawText(values[index], {itemX, centerY + 2.0f}, valueSize, color);
        ui::drawText(values[index], {itemX + 1.0f, centerY + 2.0f}, valueSize, color);
    }
    gfx::endClip();
}

float debugContentHeight(const Simulation& simulation) {
    constexpr float twoLineHeight = 22.0f + 26.0f;
    constexpr float headingHeight = 6.0f + 30.0f;
    constexpr float topPadding = 16.0f;
    constexpr int headingCount = 3;
    return static_cast<float>(debugRowCount(simulation)) * twoLineHeight +
           static_cast<float>(headingCount) * headingHeight + topPadding + 7.0f * 26.0f;
}
