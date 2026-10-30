#include "screens/MachineBenchmarkScreen.h"

#include "app/App.h"
#include "app/Paths.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <format>
#include <thread>

namespace {

constexpr float titleSize = 44.0f;
constexpr float subtitleSize = 18.0f;
constexpr float rowTextSize = 22.0f;
constexpr float listWidth = 760.0f;
constexpr float rowHeight = 56.0f;
constexpr float rowGap = 10.0f;
constexpr float progressWidth = 240.0f;
constexpr float progressHeight = 10.0f;
constexpr float buttonWidth = 200.0f;
constexpr float buttonHeight = 56.0f;
// Without vsync (hidden or automated runs) the UI loop would spin and take a core from the CPU candidates.
constexpr auto idleWhileMeasuring = std::chrono::milliseconds(12);

// The bundled Performance level the design names; the first one in the folder if it was renamed.
std::optional<BrickLayout> benchmarkLevel(std::string& error) {
    const std::vector<LevelFiles> levels = findLevels(levelDirectory(GameMode::Performance));
    if (levels.empty()) {
        error = "no Performance level to measure on";
        return std::nullopt;
    }
    auto level = loadLevel(levels.front());
    if (!level) {
        error = level.error();
        return std::nullopt;
    }
    return std::move(level->bricks);
}

} // namespace

MachineBenchmarkScreen::MachineBenchmarkScreen(App& app, HardwareFingerprint machine)
    : app(app), fingerprint(std::move(machine)) {
    std::optional<BrickLayout> level = benchmarkLevel(loadError);
    if (!level) {
        return;
    }
    BenchmarkWorkload workload;
    workload.level = std::move(*level);
    benchmark = std::make_unique<BackendBenchmark>(std::move(workload), benchmarkCandidates());
    benchmark->start();
}

void MachineBenchmarkScreen::finish(bool complete) {
    finalResults = benchmark ? benchmark->results() : std::vector<CandidateResult>{};
    winner = chooseDefaultBackend(finalResults, isBackendAvailable);
    app.machineProfile().rememberBenchmark({fingerprint, finalResults, winner, complete});
    app.adoptBenchmarkWinner(*winner);
}

void MachineBenchmarkScreen::skip() {
    if (benchmark && !winner) {
        benchmark->cancel();
        finish(false);
        app.flow().benchmarkFinished();
    }
}

void MachineBenchmarkScreen::update(float) {
    if (!winner) {
        if (!benchmark || benchmark->finished()) {
            finish(benchmark != nullptr);
            app.flow().benchmarkFinished();
        } else {
            std::this_thread::sleep_for(idleWhileMeasuring);
        }
        return;
    }
}

void MachineBenchmarkScreen::drawRow(const CandidateResult& result, float top, bool isWinner) const {
    const float left = (static_cast<float>(gfx::screenWidth()) - listWidth) * 0.5f;
    const Rect row{left, top, listWidth, rowHeight};
    ui::drawPanel(row, isWinner ? gfx::fade(palette::accent, 0.18f) : palette::panel);
    ui::drawBorder(row, 0.08f, isWinner ? palette::accent : palette::border);
    const float textY = top + (rowHeight - rowTextSize) * 0.5f;
    constexpr float padding = 20.0f;
    ui::drawText(result.candidate.label, {left + padding, textY}, rowTextSize,
                 isWinner ? palette::accent : palette::textStrong);

    const float right = left + listWidth - padding;
    if (result.status == CandidateStatus::Running) {
        const Rect track{right - progressWidth, top + (rowHeight - progressHeight) * 0.5f, progressWidth, progressHeight};
        gfx::fillRounded(track, 0.5f, palette::control);
        Rect filled = track;
        filled.width = track.width * static_cast<float>(std::clamp(result.progress, 0.0, 1.0));
        gfx::fillRounded(filled, 0.5f, palette::accent);
        return;
    }
    const Color color = result.status == CandidateStatus::Failed ? palette::danger
                        : result.status == CandidateStatus::Done ? palette::text
                                                                 : palette::textMuted;
    ui::drawText(candidateResultText(result), {right, textY}, rowTextSize, color, ui::Align::Right);
}

void MachineBenchmarkScreen::draw() {
    const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
    const float top = static_cast<float>(gfx::screenHeight()) * 0.14f;
    ui::drawText(app.flow().title(), {centerX, top}, titleSize, palette::accent, ui::Align::Center);
    ui::drawText(app.flow().footnote(),
                 {centerX, top + titleSize + 12.0f}, subtitleSize, palette::textMuted, ui::Align::Center);

    float rowTop = top + titleSize + subtitleSize + 48.0f;
    if (!loadError.empty()) {
        ui::drawText(loadError, {centerX, rowTop}, rowTextSize, palette::danger, ui::Align::Center);
        rowTop += rowHeight + rowGap;
    }
    const std::vector<CandidateResult> results = winner ? finalResults : benchmark ? benchmark->results()
                                                                                   : std::vector<CandidateResult>{};
    const std::optional<size_t> best = winner ? bestCandidate(results) : std::nullopt;
    for (size_t index = 0; index < results.size(); ++index) {
        drawRow(results[index], rowTop, best == index);
        rowTop += rowHeight + rowGap;
    }

    if (winner) {
        ui::drawText("Default backend: " + winner->label, {centerX, rowTop + 12.0f}, rowTextSize, palette::accent,
                     ui::Align::Center);
    }
    const auto buttons = app.flow().buttons();
    for (const FlowButton& button : buttons) {
        const Rect actionRect{centerX - buttonWidth * 0.5f, rowTop + 64.0f, buttonWidth, buttonHeight};
        if (ui::button(actionRect, button.label, button.enabled)) {
            app.flow().activate(button.action);
        }
    }
}

ControlBenchmarkState MachineBenchmarkScreen::controlState() const {
    return {winner ? "done" : "running",
            winner ? finalResults : benchmark ? benchmark->results() : std::vector<CandidateResult>{}, winner};
}
