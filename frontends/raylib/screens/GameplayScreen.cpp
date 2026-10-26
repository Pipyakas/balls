#include "screens/GameplayScreen.h"

#include "app/App.h"
#include "app/LaunchOptions.h"
#include "app/Paths.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "render/BoardRenderer.h"
#include "render/LauncherRenderer.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace {

constexpr float toastSeconds = 3.0f;
constexpr float overlayWidth = 420.0f;
constexpr float overlayButtonHeight = 60.0f;
constexpr float controlRowHeight = 44.0f;
constexpr float controlGap = 10.0f;
constexpr float labelColumnWidth = 150.0f;
constexpr std::array<KeyboardKey, 3> backendKeys{KEY_F1, KEY_F2, KEY_F3};

std::string spawnRateLabel(int rate) { return rate == 0 ? std::string("Max") : std::to_string(rate); }

std::string ballLimitLabel(BallCount limit) {
    if (limit == unlimitedBalls) {
        return "Max 2^64-1";
    }
    return limit >= 1'000'000 ? std::format("{}M", limit / 1'000'000) : std::format("{}k", limit / 1'000);
}

}

GameplayScreen::GameplayScreen(App& app, GameMode mode, LevelFiles files)
    : app(app), mode(mode), files(std::move(files)) {
    loadSession();
    watcher.watch(this->files);
}

void GameplayScreen::loadSession() {
    auto level = loadLevel(files);
    if (!level) {
        loadError = level.error();
        return;
    }
    loadError.clear();
    session = std::make_unique<GameSession>(mode, std::move(*level), app.preferredBackend());
}

void GameplayScreen::hotReload() {
    auto level = loadLevel(files);
    if (!level) {
        showToast("Reload failed: " + level.error(), true);
        return;
    }
    if (session) {
        session->reload(std::move(*level));
    } else {
        loadError.clear();
        session = std::make_unique<GameSession>(mode, std::move(*level), app.preferredBackend());
    }
    paused = false;
    showToast("Reloaded " + files.name, false);
}

void GameplayScreen::showToast(std::string text, bool isError) { toast = {std::move(text), toastSeconds, isError}; }

void GameplayScreen::handleShortcuts() {
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
        paused = !paused;
    }
    if (IsKeyPressed(KEY_V)) {
        app.setVsync(!app.isVsyncEnabled());
        showToast(app.isVsyncEnabled() ? "VSync on" : "VSync off", false);
    }
    if (IsKeyPressed(KEY_R)) {
        session->restart();
        paused = false;
    }
    for (size_t i = 0; i < backendKeys.size(); ++i) {
        if (IsKeyPressed(backendKeys[i])) {
            const BackendKind kind = allBackendKinds[i];
            if (session->switchBackend(kind)) {
                app.setPreferredBackend(kind);
                showToast(std::format("Backend: {}", backendKindName(kind)), false);
            } else {
                showToast(std::format("{} unavailable: {}", backendKindName(kind), backendUnavailableReason(kind)), true);
            }
        }
    }
    if (mode == GameMode::Performance && IsKeyPressed(KEY_SPACE)) {
        session->startAutoRelease();
    }
}

void GameplayScreen::handleLauncherClick(const GameplayLayout& layout) {
    if (mode != GameMode::Normal || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }
    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, layout.interactionSection)) {
        return;
    }
    const LauncherGridLayout grid = computeLauncherGrid(ui::shrink(layout.interactionSection, 12.0f),
                                                        session->launchers().columnCount(),
                                                        session->launchers().visibleRows());
    const LauncherSlotHit hit = findLauncherAt(session->launchers(), grid, mouse);
    if (hit.column >= 0) {
        session->chooseLauncher(hit.column, hit.row);
    }
}

void GameplayScreen::update(float deltaTime) {
    toast.secondsLeft = std::max(0.0f, toast.secondsLeft - deltaTime);
    if (watcher.pollChanged(std::chrono::steady_clock::now())) {
        hotReload();
    }
    if (!session) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            app.showLevelSelect(mode);
        }
        return;
    }
    handleShortcuts();
    if (paused) {
        return;
    }
    const GameplayLayout layout = computeGameplayLayout(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
        session->simulation().board().columns(), session->simulation().board().rows());
    handleLauncherClick(layout);
    session->update(deltaTime);
}

void GameplayScreen::openNextLevel() {
    const auto levels = findLevels(levelDirectory(mode));
    const auto current = std::ranges::find(levels, files.name, &LevelFiles::name);
    if (current != levels.end() && std::next(current) != levels.end()) {
        app.startGame(mode, *std::next(current));
    } else {
        app.showLevelSelect(mode);
    }
}

void GameplayScreen::drawHeader(const GameplayLayout& layout) {
    ui::drawPanel(layout.header, palette::panel, 0.25f);
    const char* modeName = mode == GameMode::Normal ? "Normal" : "Performance";
    ui::drawTextInRect(std::format("{}  -  {}", modeName, files.name), ui::shrink(layout.header, 16.0f), 28.0f,
                       palette::text, ui::Align::Left);
    const Rectangle pauseRect{layout.header.x + layout.header.width - 130.0f, layout.header.y + 8.0f, 120.0f,
                              layout.header.height - 16.0f};
    if (ui::button(pauseRect, paused ? "Resume" : "Pause")) {
        paused = !paused;
    }
}

void GameplayScreen::drawMainSection(const GameplayLayout& layout) {
    ui::drawPanel(layout.mainSection, palette::backgroundDeep, 0.02f);
    const Simulation& simulation = session->simulation();
    drawBoardBackground(simulation.board(), layout.board);
    BeginScissorMode(static_cast<int>(layout.boardRect.x), static_cast<int>(layout.boardRect.y),
                     static_cast<int>(layout.boardRect.width), static_cast<int>(layout.boardRect.height));
    drawBricks(simulation.board(), layout.board);
    drawLaunchPoint({simulation.launchX(), simulation.launchY()}, layout.board);
    app.ballRenderer().draw(session->simulation().backend().snapshot(), layout.board,
                            simulation.tuning().ballRadius);
    EndScissorMode();
}

void GameplayScreen::drawPerformanceControls(Rectangle area) {
    const int rowCount = 5;
    const Rectangle rows = ui::shrink(area, 14.0f);
    auto labeledRow = [&](int index, const char* label) {
        const Rectangle row = ui::rowSlice(rows, index, rowCount, controlGap);
        const Rectangle clamped{row.x, row.y, row.width, std::min(row.height, controlRowHeight)};
        ui::drawTextInRect(label, {clamped.x, clamped.y, labelColumnWidth, clamped.height}, 22.0f, palette::textMuted,
                           ui::Align::Left);
        return Rectangle{clamped.x + labelColumnWidth, clamped.y, clamped.width - labelColumnWidth, clamped.height};
    };

    const Rectangle startRow = labeledRow(0, "Release");
    const bool ready = session->state() == SessionState::Ready;
    const std::string startLabel =
        ready ? std::string("Start (1 click)")
              : std::format("Auto releasing - {} launchers left", session->launchers().launcherCount());
    if (ui::button(startRow, startLabel, ready)) {
        session->startAutoRelease();
    }

    const Rectangle rateRow = labeledRow(1, "Spawn / sec");
    const int rateCount = static_cast<int>(spawnRateOptions.size());
    for (int i = 0; i < rateCount; ++i) {
        if (ui::button(ui::columnSlice(rateRow, i, rateCount, controlGap), spawnRateLabel(spawnRateOptions[i]), true,
                       session->spawnRateIndex() == i)) {
            session->selectSpawnRate(i);
        }
    }

    const Rectangle multiplierRow = labeledRow(2, "Ball count");
    const int multiplierCount = static_cast<int>(ballMultiplierOptions.size());
    for (int i = 0; i < multiplierCount; ++i) {
        if (ui::button(ui::columnSlice(multiplierRow, i, multiplierCount, controlGap),
                       std::format("x{}", ballMultiplierOptions[i]), true, session->ballMultiplierIndex() == i)) {
            session->selectBallMultiplier(i);
        }
    }

    const Rectangle limitRow = labeledRow(3, "Ball limit");
    const int limitCount = static_cast<int>(ballLimitOptions.size());
    for (int i = 0; i < limitCount; ++i) {
        if (ui::button(ui::columnSlice(limitRow, i, limitCount, controlGap), ballLimitLabel(ballLimitOptions[i]), true,
                       session->ballLimitIndex() == i)) {
            session->selectBallLimit(i);
        }
    }

    const Rectangle backendRow = labeledRow(4, "Physics");
    const int backendCount = static_cast<int>(allBackendKinds.size());
    for (int i = 0; i < backendCount; ++i) {
        const BackendKind kind = allBackendKinds[i];
        if (ui::button(ui::columnSlice(backendRow, i, backendCount, controlGap),
                       std::format("{} (F{})", backendKindName(kind), i + 1), isBackendAvailable(kind),
                       session->backend() == kind) &&
            session->switchBackend(kind)) {
            app.setPreferredBackend(kind);
        }
    }
}

void GameplayScreen::drawInteractionSection(const GameplayLayout& layout) {
    ui::drawPanel(layout.interactionSection, palette::panel, 0.05f);
    if (mode == GameMode::Performance) {
        drawPerformanceControls(layout.interactionSection);
        return;
    }
    const LauncherGridLayout grid = computeLauncherGrid(ui::shrink(layout.interactionSection, 12.0f),
                                                        session->launchers().columnCount(),
                                                        session->launchers().visibleRows());
    BeginScissorMode(static_cast<int>(layout.interactionSection.x), static_cast<int>(layout.interactionSection.y),
                     static_cast<int>(layout.interactionSection.width),
                     static_cast<int>(layout.interactionSection.height));
    drawLaunchers(session->launchers(), grid, !paused && session->state() == SessionState::Running);
    EndScissorMode();
    if (session->launchers().isEmpty()) {
        ui::drawTextInRect("All launchers released", layout.interactionSection, 26.0f, palette::textMuted);
    }
}

void GameplayScreen::drawOverlay() {
    const bool won = session->state() == SessionState::Won && mode == GameMode::Normal;
    if (!paused && !won) {
        return;
    }
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(palette::backgroundDeep, 0.7f));
    const int buttonCount = 3;
    const float panelHeight = 120.0f + buttonCount * (overlayButtonHeight + 14.0f);
    const Rectangle panel{(width - overlayWidth) * 0.5f, (height - panelHeight) * 0.5f, overlayWidth, panelHeight};
    ui::drawPanel(panel, palette::panel, 0.08f);
    ui::drawText(won ? "Level Complete!" : "Paused", {panel.x + panel.width * 0.5f, panel.y + 28.0f}, 44.0f,
                 won ? palette::accent : palette::text, ui::Align::Center);
    auto overlayButton = [&](int index) {
        return Rectangle{panel.x + 30.0f, panel.y + 100.0f + static_cast<float>(index) * (overlayButtonHeight + 14.0f),
                         panel.width - 60.0f, overlayButtonHeight};
    };
    if (ui::button(overlayButton(0), won ? "Next Level" : "Resume")) {
        if (won) {
            openNextLevel();
        } else {
            paused = false;
        }
    }
    if (ui::button(overlayButton(1), "Restart")) {
        session->restart();
        paused = false;
    }
    if (ui::button(overlayButton(2), "Main Menu")) {
        app.showMainMenu();
    }
}

void GameplayScreen::drawToast() {
    if (toast.secondsLeft <= 0.0f) {
        return;
    }
    const float alpha = std::min(1.0f, toast.secondsLeft);
    const Vector2 extent = ui::measureText(toast.text, 24.0f);
    const Rectangle rect{(static_cast<float>(GetScreenWidth()) - extent.x) * 0.5f - 20.0f, 24.0f, extent.x + 40.0f,
                         48.0f};
    ui::drawPanel(rect, Fade(toast.isError ? palette::danger : palette::panelLight, alpha), 0.4f);
    ui::drawTextInRect(toast.text, rect, 24.0f, Fade(palette::text, alpha));
}

void GameplayScreen::draw() {
    if (!session) {
        const float centerX = static_cast<float>(GetScreenWidth()) * 0.5f;
        ui::drawText("Level failed to load", {centerX, 200.0f}, 40.0f, palette::danger, ui::Align::Center);
        ui::drawText(loadError, {centerX, 260.0f}, 22.0f, palette::text, ui::Align::Center);
        ui::drawText("Fix the file and save - it reloads automatically", {centerX, 300.0f}, 22.0f, palette::textMuted,
                     ui::Align::Center);
        if (ui::button({centerX - 120.0f, 360.0f, 240.0f, 56.0f}, "Back")) {
            app.showLevelSelect(mode);
        }
        drawToast();
        return;
    }
    const GameplayLayout layout = computeGameplayLayout(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
        session->simulation().board().columns(), session->simulation().board().rows());
    drawHeader(layout);
    drawMainSection(layout);
    drawInteractionSection(layout);
    drawDebugPanel(layout.debugSection, *session, app.frameTimings());
    drawOverlay();
    drawToast();
}

std::string GameplayScreen::stateJson() const {
    if (!session) {
        return std::format("{{\"loadError\":\"{}\"}}", jsonEscape(loadError));
    }
    const Simulation& simulation = session->simulation();
    const SimulationStats& stats = simulation.stats();
    std::string ballsByColor;
    for (int color = 0; color < ballColorCount; ++color) {
        ballsByColor += std::format("{}\"{}\":{}", color == 0 ? "" : ",", ballColorName(ballColorFromIndex(color)),
                                    stats.ballsByColor[color]);
    }
    std::string launcherColumns;
    const LauncherQueue& launchers = session->launchers();
    for (int column = 0; column < launchers.columnCount(); ++column) {
        launcherColumns += column == 0 ? "[" : ",[";
        for (int row = 0; row < launchers.visibleRows(); ++row) {
            const QueuedLauncher* queued = launchers.visible(column, row);
            if (!queued) {
                break;
            }
            launcherColumns += std::format("{}{{\"color\":\"{}\",\"balls\":{}}}", row == 0 ? "" : ",",
                                           ballColorName(queued->launcher.color), queued->launcher.ballCount);
        }
        launcherColumns += "]";
    }
    constexpr std::array<const char*, 3> stateNames{"ready", "running", "won"};
    return std::format(
        "{{\"mode\":\"{}\",\"level\":\"{}\",\"state\":\"{}\",\"paused\":{},\"backend\":\"{}\",\"backendFault\":\"{}\","
        "\"balls\":{},\"bricks\":{},\"launchersLeft\":{},\"pendingSpawn\":{},\"spawned\":{},\"hits\":{},"
        "\"physicsMs\":{:.3f},\"ticks\":{},\"spawnRate\":{},\"multiplier\":{},\"ballLimit\":{},\"elapsed\":{:.2f},"
        "\"ballsByColor\":{{{}}},\"launchers\":[{}]}}",
        mode == GameMode::Normal ? "normal" : "performance", jsonEscape(files.name),
        stateNames[static_cast<int>(session->state())], paused, backendKindName(session->backend()),
        jsonEscape(session->lastBackendFault()), stats.ballCount, simulation.board().aliveBrickCount(),
        launchers.launcherCount(), session->spawner().pendingBalls(), stats.totalBallsSpawned, stats.totalHits,
        stats.lastFramePhysicsMs, stats.ticksLastFrame, spawnRateOptions[session->spawnRateIndex()],
        ballMultiplierOptions[session->ballMultiplierIndex()], session->ballLimit(), session->elapsedSeconds(), ballsByColor,
        launcherColumns);
}

std::optional<ControlResponse> GameplayScreen::handleControl(const ControlRequest& request) {
    if (!session) {
        return std::nullopt;
    }
    const std::string& path = request.path;
    const ControlResponse done = ControlResponse::ok("{\"ok\":true}");
    if (path == "/launch") {
        const bool released = session->chooseLauncher(request.integer("column").value_or(0), request.integer("row").value_or(0));
        return released ? done : ControlResponse::error(409, "no launcher there or game not running");
    }
    if (path == "/start") {
        session->startAutoRelease();
        return done;
    }
    if (path == "/rate") {
        session->selectSpawnRate(request.integer("index").value_or(session->spawnRateIndex()));
        return done;
    }
    if (path == "/limit") {
        session->selectBallLimit(request.integer("index").value_or(session->ballLimitIndex()));
        return done;
    }
    if (path == "/multiplier") {
        session->selectBallMultiplier(request.integer("index").value_or(session->ballMultiplierIndex()));
        return done;
    }
    if (path == "/backend") {
        const auto kind = parseBackendKind(request.text("kind").value_or(""));
        if (!kind || !session->switchBackend(*kind)) {
            return ControlResponse::error(409, "backend unknown or unavailable");
        }
        app.setPreferredBackend(*kind);
        return done;
    }
    if (path == "/pause") {
        paused = true;
        return done;
    }
    if (path == "/resume") {
        paused = false;
        return done;
    }
    if (path == "/restart") {
        session->restart();
        paused = false;
        return done;
    }
    return std::nullopt;
}
