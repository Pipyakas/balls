#include "screens/GameplayScreen.h"

#include "app/App.h"
#include "app/LaunchOptions.h"
#include "app/Paths.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "render/BoardRenderer.h"
#include "render/LauncherRenderer.h"
#include "render/Palette.h"
#include "screens/SettingsPanel.h"
#include "ui/Widgets.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>

namespace {

constexpr float controlsScrollStep = 40.0f;

constexpr float toastSeconds = 3.0f;
constexpr float mainSectionCornerRadius = 0.0f;
constexpr float overlayWidth = 420.0f;
constexpr float overlayButtonHeight = 60.0f;
constexpr float controlRowHeight = 44.0f;
constexpr float controlGap = 10.0f;
constexpr float groupLabelHeight = 26.0f;
constexpr float groupGap = 14.0f;
constexpr float minimumControlColumnWidth = 220.0f;
constexpr float controlColumnGap = 16.0f;
constexpr float probeStatusHeight = 28.0f;
constexpr float advancedHeaderHeight = 40.0f;
constexpr float probeStatusSize = 17.0f;

float listHeight(size_t rows) {
    return (controlRowHeight + controlGap) * static_cast<float>(rows) - controlGap;
}

constexpr double millisecondsPerSecond = 1000.0;
constexpr int minimumProbeTargetFps = 10;
constexpr int maximumProbeTargetFps = 1000;
constexpr int defaultTargetFps = 60;

std::string spawnRateLabel(int rate) {
    return rate == 0 ? std::string("Max") : std::to_string(rate);
}

std::string ballLimitLabel(BallCount limit) {
    if (limit == unlimitedBalls) {
        return "Max 2^64-1";
    }
    return limit >= 1'000'000 ? std::format("{}M", limit / 1'000'000) : std::format("{}k", limit / 1'000);
}

std::string ballLimitOptionLabel(BallCount limit) {
    return ballLimitLabel(limit);
}

std::string groupedBalls(BallCount value) {
    std::string digits = std::to_string(value);
    for (int insertAt = static_cast<int>(digits.size()) - 3; insertAt > 0; insertAt -= 3) {
        digits.insert(static_cast<size_t>(insertAt), ",");
    }
    return digits;
}

std::string probeStatus(const SessionView& view) {
    const CapacityProbe& probe = view.probe;
    if (probe.isActive()) {
        return std::format("Probing... {} balls, {:.0f} fps", groupedBalls(view.liveBalls), probe.lastFps());
    }
    if (probe.phase() == ProbePhase::Done) {
        return std::format("Max @{:.0f} FPS: {}", probe.targetFps(), groupedBalls(probe.bestPassingCount()));
    }
    if (const auto measured = view.machineLimit) {
        return std::format("Max: {}", groupedBalls(*measured));
    }
    return {};
}

} // namespace

GameplayScreen::GameplayScreen(App& app, GameMode mode, LevelFiles files)
    : app(app), mode(mode), files(std::move(files)), probeTargetFps(defaultTargetFps) {
    loadSession();
    refreshSettings();
    watcher.watch(this->files);
}

void GameplayScreen::openSettingsOverlay() {
    paused = true;
    settingsOpen = true;
}

void GameplayScreen::refreshSettings() {
    if (!runner) {
        return;
    }
    const bool collisions = app.settings().ballCollisions;
    runner->post([collisions](GameSession& session) {
        session.simulation().setBallCollisions(collisions);
        if (collisions && !session.simulation().ballCollisionsActive()) {
            session.requestBackend(BackendKind::Scalar);
        }
    });
    if (collisions && view && !view->collisionsActive) {
        showToast("Ball collisions need the Scalar backend - switching to Scalar", false);
    }
}

void GameplayScreen::loadSession() {
    diskState = DiskState::Unchanged;
    auto level = loadLevel(files);
    if (!level) {
        runner.reset();
        view.reset();
        loadError = level.error();
        return;
    }
    loadError.clear();
    view.reset();
    runner = std::make_unique<SessionRunner>(mode, std::move(*level), app.preferredBackend(),
                                              app.settings().ticksPerSecond);
    applyDefaultBallLimit();
}

void GameplayScreen::applyDefaultBallLimit() {
    if (mode == GameMode::Performance) {
        useAutoBallLimit();
    }
}

// Auto means the most balls this machine holds at the target frame rate: the remembered probe result when it was
// measured at this target, otherwise a new probe.
// The remembered limit depends on the backend, which the session may only settle on the physics thread; the
// profile is the UI's, so the choice is made there, in the command, from a copy of it.
void GameplayScreen::useAutoBallLimit() {
    const MachineProfile profile = app.machineProfile();
    const int targetFps = probeTargetFps;
    runner->post([profile, targetFps](GameSession& session) {
        const auto remembered = profile.ballLimit(session.backend());
        if (remembered && static_cast<int>(remembered->targetFps) == targetFps) {
            session.useMachineBallLimit(remembered->balls);
        } else {
            session.startCapacityProbe(static_cast<double>(targetFps));
        }
    });
}

void GameplayScreen::handleSessionEvents() {
    for (const SessionEvent& event : runner->takeEvents()) {
        if (event.kind == SessionEventKind::BackendSwitched) {
            applyDefaultBallLimit();
        } else {
            showToast(std::format("Could not start the {} backend", backendKindName(event.backend)), true);
        }
    }
}

void GameplayScreen::startProbe() {
    const double targetFps = static_cast<double>(probeTargetFps);
    runner->post([targetFps](GameSession& session) { session.startCapacityProbe(targetFps); });
}

void GameplayScreen::rememberFinishedProbe() {
    const CapacityProbe& probe = view->probe;
    if (probe.isActive()) {
        probeResultSaved = false;
        return;
    }
    if (!probeResultSaved && probe.phase() == ProbePhase::Done) {
        app.machineProfile().rememberBallLimit(view->backend, probe.targetFps(),
                                               probe.bestPassingCount());
        showToast(std::format("{} limit on this machine: {} balls @{:.0f} FPS",
                              backendKindName(view->backend), groupedBalls(probe.bestPassingCount()),
                              probe.targetFps()),
                  false);
        probeResultSaved = true;
    }
}

void GameplayScreen::noticeDiskChange() {
    const bool present = std::filesystem::exists(files.bricksPath) && std::filesystem::exists(files.launchersPath);
    diskState = present ? DiskState::Changed : DiskState::Removed;
}

void GameplayScreen::restartLevel() {
    paused = false;
    if (diskState == DiskState::Removed) {
        app.showLevelSelect(mode);
    } else if (diskState == DiskState::Changed) {
        loadSession();
    } else {
        runner->post([](GameSession& session) { session.restart(); });
    }
}

std::string_view GameplayScreen::diskNotice() const {
    switch (diskState) {
    case DiskState::Changed:
        return "Map changed on disk - Restart to load it";
    case DiskState::Removed:
        return "Map removed from disk - Restart unavailable";
    case DiskState::Unchanged:
        break;
    }
    return {};
}

void GameplayScreen::showToast(std::string text, bool isError) {
    toast = {std::move(text), toastSeconds, isError};
}

float portraitLauncherPressSlop() {
    return 8.0f;
}

float clampPortraitScroll(float scroll, float rowWidth, float areaWidth) {
    const float overflow = rowWidth - areaWidth;
    if (overflow <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(scroll, 0.0f, overflow);
}

void GameplayScreen::handleLauncherClick(const GameplayLayout& layout) {
    if (mode != GameMode::Normal) {
        portraitPressing = false;
        portraitDragged = false;
        return;
    }
    if (input::mouseReleased(input::MouseButton::Left) && portraitPressing && layout.isPortrait) {
        const Vec2 released = input::mousePosition();
        if (!portraitDragged && pointInRect(released, layout.interactionSection)) {
            const LauncherGridLayout grid =
                computePortraitLauncherGrid(ui::shrink(layout.interactionSection, 12.0f),
                                            view->launchers.columnCount(), view->launchers.visibleRows());
            const LauncherSlotHit hit =
                findPortraitLauncherAt(view->launchers, grid, portraitLauncherScroll, released);
            if (hit.column >= 0) {
                runner->post([hit](GameSession& session) { session.chooseLauncher(hit.column, hit.row); });
            }
        }
        portraitPressing = false;
        portraitDragged = false;
    }
    if (!input::mousePressed(input::MouseButton::Left)) {
        return;
    }
    const Vec2 mouse = input::mousePosition();
    if (!pointInRect(mouse, layout.interactionSection)) {
        return;
    }
    if (layout.isPortrait) {
        portraitPressing = true;
        portraitDragged = false;
        portraitPressStart = mouse;
        return;
    }
    const LauncherGridLayout grid =
        computeLauncherGrid(ui::shrink(layout.interactionSection, 12.0f), view->launchers.columnCount(),
                            view->launchers.visibleRows());
    const LauncherSlotHit hit = findLauncherAt(view->launchers, grid, mouse);
    if (hit.column >= 0) {
        runner->post([hit](GameSession& session) { session.chooseLauncher(hit.column, hit.row); });
    }
}

void GameplayScreen::update(float deltaTime) {
    toast.secondsLeft = std::max(0.0f, toast.secondsLeft - deltaTime);
    if (watcher.pollChanged(std::chrono::steady_clock::now())) {
        if (runner) {
            noticeDiskChange();
        } else {
            loadSession();
        }
    }
    if (!runner) {
        if (input::keyPressed(SDL_SCANCODE_ESCAPE)) {
            app.showLevelSelect(mode);
        }
        return;
    }
    view = runner->view();
    handleSessionEvents();
    if (!view) {
        runner->tick({paused, 0.0, 0.0, 0.0f});
        return;
    }
    const GameplayLayout layout = computeGameplayLayout(
        {static_cast<float>(gfx::screenWidth()), static_cast<float>(gfx::screenHeight())}, view->board->columns(),
        view->board->rows(), app.settings().layout, mode == GameMode::Normal ? view->launchers.visibleRows() : 0);
    if (!paused) {
        handleLauncherClick(layout);
    }
    tickPhysics(layout);
    rememberFinishedProbe();
}

// The physics thread keeps its own pace; this hands it the pause state, the UI's frame timings for the capacity probe
// and the visibility slot size. The view drawn this frame was taken above, so commands posted while drawing show
// from a later view.
void GameplayScreen::tickPhysics(const GameplayLayout& layout) {
    const FrameSample& lastFrame = app.frameTimings().latest();
    PhysicsFrameInput input;
    input.paused = paused;
    input.uiFrameSeconds = lastFrame.workMs() / millisecondsPerSecond;
    input.uiWorkSeconds = std::max(lastFrame.cpuMs, lastFrame.gpuMs) / millisecondsPerSecond;
    input.visibilitySlotCells = BallView::slotCells(layout.board, view->ballRadius);
    input.drawing = app.settings().drawBoard;
    runner->tick(input);
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

// The whole header is the Pause / Resume control. It names the action only while hovered or pressed, so the rest of
// the time the title has the full width; a title still too long scrolls sideways rather than shrinking.
void GameplayScreen::drawHeader(const GameplayLayout& layout) {
    constexpr float titleInset = 14.0f;
    const Rect header = layout.header;
    const bool hovered = ui::isHovered(header);
    const bool pressed = hovered && input::mouseDown(input::MouseButton::Left);
    ui::drawPanel(header, pressed ? palette::controlPressed : (hovered ? palette::controlHover : palette::panel), 0.25f);
    if (hovered) {
        ui::drawTextInRect(paused ? "Resume" : "Pause", header, 22.0f, palette::textStrong);
    } else {
        const char* modeName = mode == GameMode::Normal ? "Normal" : "Performance";
        const std::string title = std::format("{} - {}", modeName, files.name);
        const std::string_view notice = diskNotice();
        const Rect titleRect{header.x + titleInset, header.y, header.width - titleInset * 2.0f, header.height};
        if (notice.empty()) {
            ui::drawScrollingText(title, titleRect, 22.0f, palette::text);
        } else {
            const float halfHeight = titleRect.height * 0.5f;
            ui::drawScrollingText(title, {titleRect.x, titleRect.y + 4.0f, titleRect.width, halfHeight - 4.0f}, 18.0f,
                                  palette::text);
            ui::drawScrollingText(notice, {titleRect.x, titleRect.y + halfHeight, titleRect.width, halfHeight - 4.0f},
                                  15.0f, palette::accent);
        }
    }
    if (hovered && input::mouseReleased(input::MouseButton::Left)) {
        paused = !paused;
    }
}

void GameplayScreen::drawMainSection(const GameplayLayout& layout) {
    gfx::sceneRect(layout.mainSection, palette::backgroundDeep, mainSectionCornerRadius);
    if (!app.settings().drawBoard) {
        ui::drawTextInRect("Board drawing off - physics only (Settings > Advanced)", layout.mainSection, 18.0f,
                           palette::textMuted, ui::Align::Center);
        return;
    }
    drawBoardBackground(*view->board, layout.board);
    gfx::beginClip(layout.boardRect);
    drawBricks(*view->board, layout.board);
    drawLaunchPoint({view->launchX, view->launchY}, layout.board);
    app.ballRenderer().draw(view->balls, layout.board, view->ballRadius);
    gfx::endClip();
}

namespace {

// Menu label for an adapter: the vendor prefix adds width, not information.
std::string shortAdapterName(const GpuAdapterInfo& adapter) {
    std::string name = adapter.name;
    for (const std::string_view prefix : {"NVIDIA GeForce ", "NVIDIA ", "AMD Radeon(TM) ", "AMD Radeon ", "AMD ",
                                          "Intel(R) ", "Microsoft "}) {
        if (name.starts_with(prefix)) {
            name.erase(0, prefix.size());
            break;
        }
    }
    if (adapter.kind == GpuKind::Software) {
        name += " (CPU)";
    } else if (adapter.kind == GpuKind::Integrated) {
        name += " (iGPU)";
    }
    return name;
}

}

void GameplayScreen::selectComputeAdapter(int adapter) {
    setComputeGpuPreference(std::to_string(adapter));
    refreshBackendStatuses();
    if (view->backend == BackendKind::Vulkan || view->pendingBackend == BackendKind::Vulkan) {
        runner->post([](GameSession& session) { session.requestBackend(BackendKind::Vulkan); });
        showToast("Vulkan physics on " + app.computeAdapters()[static_cast<size_t>(adapter)].name, false);
    }
}

std::vector<int> GameplayScreen::renderAdapterChoices() const {
    std::vector<int> choices;
    for (int i = 0; i < static_cast<int>(app.renderAdapters().size()); ++i) {
        if (app.renderAdapters()[static_cast<size_t>(i)].usableForRender()) {
            choices.push_back(i);
        }
    }
    return choices;
}

std::vector<int> GameplayScreen::computeAdapterChoices() const {
    std::vector<int> choices;
    for (int i = 0; i < static_cast<int>(app.computeAdapters().size()); ++i) {
        if (app.computeAdapters()[static_cast<size_t>(i)].usableForCompute()) {
            choices.push_back(i);
        }
    }
    return choices;
}

void GameplayScreen::drawAdapterList(Rect area, const std::vector<GpuAdapterInfo>& adapters,
                                     std::span<const int> choices, int selected, bool forCompute) {
    float cursor = area.y;
    for (const int adapter : choices) {
        if (ui::button({area.x, cursor, area.width, controlRowHeight},
                       shortAdapterName(adapters[static_cast<size_t>(adapter)]), true, selected == adapter)) {
            if (forCompute) {
                selectComputeAdapter(adapter);
            } else {
                app.requestRenderAdapter(adapter);
            }
        }
        cursor += controlRowHeight + controlGap;
    }
}

void GameplayScreen::drawBackendList(Rect area, BackendKind selected) {
    float cursor = area.y;
    for (BackendKind kind : allBackendKinds) {
        const BackendStatus status = backendStatus(kind);
        const bool available = status.available;
        const bool starting = view->pendingBackend == kind;
        std::string label(backendKindName(kind));
        if (!available) {
            label += std::string(" - ") + status.unavailableReason;
        } else if (starting) {
            label += " - starting...";
        }
        const bool pressed = ui::button({area.x, cursor, area.width, controlRowHeight}, label, available,
                                        starting || (selected == kind && !view->pendingBackend));
        if (pressed && available) {
            runner->post([kind](GameSession& session) { session.requestBackend(kind); });
        }
        cursor += controlRowHeight + controlGap;
    }
}

int GameplayScreen::drawOptionDropdown(Rect area, std::string_view current,
                                       std::span<const std::string> options, int selected, bool& open) {
    const Rect row{area.x, area.y, area.width, controlRowHeight};
    int picked = -1;
    ui::dropdown(row, current, open);
    float cursor = area.y + controlRowHeight + controlGap;
    if (open) {
        const Rect list{area.x, area.y, area.width,
                        controlRowHeight + (controlRowHeight + controlGap) * static_cast<float>(options.size())};
        for (size_t i = 0; i < options.size(); ++i) {
            const Rect option{area.x, cursor, area.width, controlRowHeight};
            if (ui::button(option, options[i], true, static_cast<int>(i) == selected)) {
                picked = static_cast<int>(i);
            }
            cursor += controlRowHeight + controlGap;
        }
        if (picked >= 0) {
            open = false;
        } else if (!pointInRect(input::mousePosition(), list) &&
                   input::mousePressed(input::MouseButton::Left)) {
            open = false;
        }
    }
    return picked;
}

void GameplayScreen::drawPerformanceControls(Rect area) {
    const Rect inner = ui::shrink(area, 14.0f);
    float visibleHeight = 0.0f;
    ui::scrollFrame(inner, controlsScroll, performanceControlsHeight, visibleHeight);
    gfx::beginClip(inner);
    const float top = inner.y - controlsScroll;
    const float contentWidth = inner.width;
    const int columnCount = contentWidth >= minimumControlColumnWidth * 2.0f + controlColumnGap ? 2 : 1;
    const float columnWidth = (contentWidth - controlColumnGap * static_cast<float>(columnCount - 1)) /
                              static_cast<float>(columnCount);
    std::array<float, 2> columnCursors{top, top};
    auto group = [&](const char* label, float contentHeight) {
        const size_t column = columnCount == 1 || columnCursors[0] <= columnCursors[1] ? 0 : 1;
        float& cursor = columnCursors[column];
        const float x = inner.x + static_cast<float>(column) * (columnWidth + controlColumnGap);
        ui::drawText(label, {x, cursor}, 20.0f, palette::textMuted);
        cursor += groupLabelHeight;
        const Rect rows{x, cursor, columnWidth, contentHeight};
        cursor += contentHeight + groupGap;
        return rows;
    };

    const bool ready = view->state == SessionState::Ready;
    const std::string startLabel =
        ready ? std::string("Start (1 click)")
              : std::format("Auto releasing - {} launchers left", view->launchers.launcherCount());
    if (ui::button(group("Release", controlRowHeight), startLabel, ready)) {
        runner->post([](GameSession& session) { session.startAutoRelease(); });
    }

    static const std::array<std::string, spawnRateOptions.size()> spawnLabels = [] {
        std::array<std::string, spawnRateOptions.size()> labels{};
        for (size_t i = 0; i < spawnRateOptions.size(); ++i) {
            labels[i] = spawnRateLabel(spawnRateOptions[i]);
        }
        return labels;
    }();
    const float spawnHeight =
        controlRowHeight + (spawnDropdownOpen ? (controlRowHeight + controlGap) * spawnLabels.size() : 0.0f);
    const int pickedSpawn =
        drawOptionDropdown(group("Spawn / sec", spawnHeight), spawnLabels[view->spawnRateIndex],
                           spawnLabels, view->spawnRateIndex, spawnDropdownOpen);
    if (pickedSpawn >= 0) {
        runner->post([pickedSpawn](GameSession& session) { session.selectSpawnRate(pickedSpawn); });
    }

    static const std::array<std::string, ballMultiplierOptions.size()> countLabels = [] {
        std::array<std::string, ballMultiplierOptions.size()> labels{};
        for (size_t i = 0; i < ballMultiplierOptions.size(); ++i) {
            labels[i] = std::format("x{}", ballMultiplierOptions[i]);
        }
        return labels;
    }();
    const float countHeight =
        controlRowHeight + (countDropdownOpen ? (controlRowHeight + controlGap) * countLabels.size() : 0.0f);
    const int pickedCount =
        drawOptionDropdown(group("Ball count", countHeight), countLabels[view->ballMultiplierIndex],
                           countLabels, view->ballMultiplierIndex, countDropdownOpen);
    if (pickedCount >= 0) {
        runner->post([pickedCount](GameSession& session) { session.selectBallMultiplier(pickedCount); });
    }

    static const std::array<std::string, ballLimitOptions.size() + 1> limitLabels = [] {
        std::array<std::string, ballLimitOptions.size() + 1> labels{};
        labels[0] = "Auto";
        for (size_t i = 0; i < ballLimitOptions.size(); ++i) {
            labels[i + 1] = ballLimitOptionLabel(ballLimitOptions[i]);
        }
        return labels;
    }();
    const bool autoLimit = view->usingMachineLimit || view->probe.isActive();
    const int selectedLimit = autoLimit ? 0 : view->ballLimitIndex + 1;
    const float dropdownHeight =
        controlRowHeight + (limitDropdownOpen ? (controlRowHeight + controlGap) * limitLabels.size() : 0.0f);
    const float autoRowsHeight = autoLimit ? controlGap + controlRowHeight + probeStatusHeight : 0.0f;
    const Rect limitArea = group("Ball limit", dropdownHeight + autoRowsHeight);
    const int pickedLimit = drawOptionDropdown(limitArea, limitLabels[selectedLimit], limitLabels, selectedLimit,
                                               limitDropdownOpen);
    if (pickedLimit == 0) {
        useAutoBallLimit();
    } else if (pickedLimit > 0) {
        runner->post([pickedLimit](GameSession& session) {
            session.cancelCapacityProbe();
            session.selectBallLimit(pickedLimit - 1);
        });
    }
    if (autoLimit && pickedLimit < 0) {
        const float stepperY = limitArea.y + dropdownHeight + controlGap;
        if (ui::stepper({limitArea.x, stepperY, limitArea.width, controlRowHeight}, "Target FPS", probeTargetFps,
                        minimumProbeTargetFps, maximumProbeTargetFps)) {
            startProbe();
        }
        ui::drawTextInRect(probeStatus(*view),
                           {limitArea.x, stepperY + controlRowHeight, limitArea.width, probeStatusHeight},
                           probeStatusSize, palette::textMuted, ui::Align::Left);
    }

    // The section header spans both columns, under whichever column ended lower.
    const float headerY = std::max(columnCursors[0], columnCursors[1]);
    const bool advancedOpen = app.settings().advancedSettings;
    if (ui::collapsingHeader({inner.x, headerY, contentWidth, advancedHeaderHeight}, "Advanced Settings", advancedOpen)) {
        app.setAdvancedSettingsOpen(!advancedOpen);
    }
    columnCursors = {headerY + advancedHeaderHeight + groupGap, headerY + advancedHeaderHeight + groupGap};
    if (advancedOpen) {
        const float backendHeight = (controlRowHeight + controlGap) * allBackendKinds.size() - controlGap;
        drawBackendList(group("Physics", backendHeight), view->backend);
        if (const std::vector<int> choices = renderAdapterChoices(); !choices.empty()) {
            drawAdapterList(group("Render GPU", listHeight(choices.size())), app.renderAdapters(), choices,
                            app.renderAdapterIndex(), false);
        }
        if (const std::vector<int> choices = computeAdapterChoices(); !choices.empty()) {
            drawAdapterList(group("Compute GPU (Vulkan backend)", listHeight(choices.size())), app.computeAdapters(),
                            choices, selectGpuAdapter(app.computeAdapters(), computeGpuPreference(), GpuRole::Compute),
                            true);
        }
    }
    performanceControlsHeight = std::max(columnCursors[0], columnCursors[1]) - top - groupGap;
    ui::scrollHints(inner, controlsScroll, performanceControlsHeight, palette::panel);
    gfx::endClip();
}

float GameplayScreen::normalContentHeight() const {
    const float launcherHeight = 320.0f;
    const float advancedHeight = app.settings().advancedSettings
                                     ? groupGap + groupLabelHeight +
                                           (controlRowHeight + controlGap) * allBackendKinds.size() - controlGap
                                     : 0.0f;
    return launcherHeight + groupGap + advancedHeaderHeight + advancedHeight;
}

void GameplayScreen::drawPortraitLauncherRow(Rect area) {
    constexpr float wheelStep = 40.0f;
    const Rect inner = ui::shrink(area, 12.0f);
    const LauncherGridLayout grid = computePortraitLauncherGrid(inner, view->launchers.columnCount(),
                                                                view->launchers.visibleRows());
    const float rowWidth = portraitRowWidth(view->launchers, grid);
    if (pointInRect(input::mousePosition(), inner)) {
        portraitLauncherScroll = clampPortraitScroll(portraitLauncherScroll - input::wheelMove() * wheelStep,
                                                     rowWidth, inner.width);
    }
    if (portraitPressing && input::mouseDown(input::MouseButton::Left) &&
        pointInRect(input::mousePosition(), inner)) {
        const Vec2 mouse = input::mousePosition();
        const float dragged =
            std::abs(mouse.x - portraitPressStart.x) + std::abs(mouse.y - portraitPressStart.y);
        if (dragged > portraitLauncherPressSlop()) {
            portraitDragged = true;
        }
        if (portraitDragged) {
            portraitLauncherScroll = clampPortraitScroll(
                portraitLauncherScroll - (mouse.x - portraitPressStart.x), rowWidth, inner.width);
            portraitPressStart = mouse;
        }
    }
    gfx::beginClip(inner);
    drawPortraitLaunchers(view->launchers, grid, portraitLauncherScroll,
                          !paused && view->state == SessionState::Running);
    if (view->launchers.isEmpty()) {
        ui::drawTextInRect("All launchers released", inner, 26.0f, palette::textMuted);
    }
    ui::horizontalScrollHints(inner, portraitLauncherScroll, rowWidth, palette::panel);
    gfx::endClip();
}

void GameplayScreen::drawInteractionSection(const GameplayLayout& layout) {
    ui::drawPanel(layout.interactionSection, palette::panel, 0.05f);
    if (mode == GameMode::Performance) {
        drawPerformanceControls(layout.interactionSection);
        return;
    }
    if (layout.isPortrait) {
        drawPortraitLauncherRow(layout.interactionSection);
        return;
    }
    drawNormalControls(layout.interactionSection);
}

void GameplayScreen::drawNormalControls(Rect area) {
    const Rect inner = ui::shrink(area, 14.0f);
    float visibleHeight = 0.0f;
    ui::scrollFrame(inner, normalScroll, normalContentHeight(), visibleHeight);
    gfx::beginClip(inner);
    const float width = inner.width;
    float cursor = inner.y - normalScroll;
    const Rect launcherArea{inner.x, cursor, width, 320.0f};
    const LauncherGridLayout grid =
        computeLauncherGrid(ui::shrink(launcherArea, 12.0f), view->launchers.columnCount(),
                            view->launchers.visibleRows());
    drawLaunchers(view->launchers, grid, !paused && view->state == SessionState::Running);
    if (view->launchers.isEmpty()) {
        ui::drawTextInRect("All launchers released", launcherArea, 26.0f, palette::textMuted);
    }
    cursor += 320.0f + groupGap;
    const bool advancedOpen = app.settings().advancedSettings;
    if (ui::collapsingHeader({inner.x, cursor, width, advancedHeaderHeight}, "Advanced Settings", advancedOpen)) {
        app.setAdvancedSettingsOpen(!advancedOpen);
    }
    cursor += advancedHeaderHeight + groupGap;
    if (advancedOpen) {
        ui::drawText("Physics", {inner.x, cursor}, 20.0f, palette::textMuted);
        cursor += groupLabelHeight;
        drawBackendList(
            {inner.x, cursor, width, (controlRowHeight + controlGap) * allBackendKinds.size() - controlGap},
            view->backend);
    }
    ui::scrollHints(inner, normalScroll, normalContentHeight(), palette::panel);
    gfx::endClip();
}

void GameplayScreen::drawOverlay() {
    const bool won = view->state == SessionState::Won && mode == GameMode::Normal;
    if (!paused && !won) {
        return;
    }
    if (input::keyPressed(SDL_SCANCODE_ESCAPE) && settingsOpen) {
        settingsOpen = false;
    }
    const float width = static_cast<float>(gfx::screenWidth());
    const float height = static_cast<float>(gfx::screenHeight());
    gfx::fillRect({0.0f, 0.0f, width, height}, gfx::fade(palette::backgroundDeep, 0.7f));
    if (settingsOpen) {
        const Rect panel{(width - settingsPanelWidth) * 0.5f, (height - settingsPanelHeight) * 0.5f,
                         settingsPanelWidth, settingsPanelHeight};
        ui::drawPanel(panel, palette::panel, 0.08f);
        const SettingsPanelEdit edit =
            drawSettingsPanel(panel, app.settings(), app.preferredBackend(), app.benchmarkScores(),
                              app.isWebGpuSupported(), pauseSettingsScroll);
        if (edit.changed) {
            app.applySettings(edit.settings, edit.backend);
        }
        if (edit.runBenchmark) {
            app.showMachineBenchmark();
        }
        if (edit.backPressed) {
            settingsOpen = false;
        }
        return;
    }
    const int buttonCount = 4;
    const float panelHeight = 120.0f + buttonCount * (overlayButtonHeight + 14.0f);
    const Rect panel{(width - overlayWidth) * 0.5f, (height - panelHeight) * 0.5f, overlayWidth, panelHeight};
    ui::drawPanel(panel, palette::panel, 0.08f);
    ui::drawText(won ? "Level Complete!" : "Paused", {panel.x + panel.width * 0.5f, panel.y + 28.0f}, 44.0f,
                 won ? palette::accent : palette::text, ui::Align::Center);
    auto overlayButton = [&](int index) {
        return Rect{panel.x + 30.0f,
                    panel.y + 100.0f + static_cast<float>(index) * (overlayButtonHeight + 14.0f),
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
        restartLevel();
    }
    if (ui::button(overlayButton(2), "Settings")) {
        settingsOpen = true;
    }
    if (ui::button(overlayButton(3), "Main Menu")) {
        app.showMainMenu();
    }
}

void GameplayScreen::drawToast() {
    if (toast.secondsLeft <= 0.0f) {
        return;
    }
    const float alpha = std::min(1.0f, toast.secondsLeft);
    const Vec2 extent = ui::measureText(toast.text, 24.0f);
    const Rect rect{(static_cast<float>(gfx::screenWidth()) - extent.x) * 0.5f - 20.0f, 24.0f,
                    extent.x + 40.0f, 48.0f};
    ui::drawPanel(rect, gfx::fade(toast.isError ? palette::danger : palette::panelLight, alpha), 0.4f);
    ui::drawTextInRect(toast.text, rect, 24.0f, gfx::fade(palette::text, alpha));
}

void GameplayScreen::draw() {
    if (!runner) {
        const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
        ui::drawText("Level failed to load", {centerX, 200.0f}, 40.0f, palette::danger, ui::Align::Center);
        ui::drawText(loadError, {centerX, 260.0f}, 22.0f, palette::text, ui::Align::Center);
        ui::drawText("Fix the file and save - it reloads automatically", {centerX, 300.0f}, 22.0f,
                     palette::textMuted, ui::Align::Center);
        if (ui::button({centerX - 120.0f, 360.0f, 240.0f, 56.0f}, "Back")) {
            app.showLevelSelect(mode);
        }
        drawToast();
        return;
    }
    if (!view) {
        const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
        ui::drawText("Loading " + files.name, {centerX, static_cast<float>(gfx::screenHeight()) * 0.45f}, 32.0f,
                     palette::textMuted, ui::Align::Center);
        return;
    }
    const GameplayLayout layout = computeGameplayLayout(
        {static_cast<float>(gfx::screenWidth()), static_cast<float>(gfx::screenHeight())}, view->board->columns(),
        view->board->rows(), app.settings().layout, mode == GameMode::Normal ? view->launchers.visibleRows() : 0);
    drawHeader(layout);
    if (layout.isPortrait) {
        ui::drawPanel(layout.topGadget, palette::panel, 0.25f);
        drawHeader(layout);
        drawPortraitDebugStrip(layout.debugSection, *view, app.frameTimings(), app.settings().advancedSettings);
    }
    drawMainSection(layout);
    drawInteractionSection(layout);
    if (!layout.isPortrait) {
        drawDebugPanel(layout.debugSection, *view, app.frameTimings(), debugState, app.settings().advancedSettings);
    }
    drawOverlay();
    drawToast();
}

std::string GameplayScreen::stateJson() const {
    if (!runner) {
        return std::format("{{\"loadError\":\"{}\"}}", jsonEscape(loadError));
    }
    const std::shared_ptr<const SessionView> view = runner->view();
    if (!view) {
        return "{\"loading\":true}";
    }
    const SimulationStats& stats = view->stats;
    std::string ballsByColor;
    for (int color = 0; color < ballColorCount; ++color) {
        ballsByColor += std::format("{}\"{}\":{}", color == 0 ? "" : ",",
                                    ballColorName(ballColorFromIndex(color)), stats.ballsByColor[color]);
    }
    std::string launcherColumns;
    const LauncherQueue& launchers = view->launchers;
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
    const CapacityProbe& probe = view->probe;
    return std::format(
        "{{\"mode\":\"{}\",\"level\":\"{}\",\"diskNotice\":\"{}\",\"state\":\"{}\",\"paused\":{},\"backend\":\"{}\",\"pendingBackend\":\"{}\",\"refilling\":{},\"backendFault\":\"{}\","
        "\"balls\":{},\"bricks\":{},\"launchersLeft\":{},\"pendingSpawn\":{},\"spawned\":{},\"hits\":{},"
        "\"physicsMs\":{:.3f},\"kernelMs\":{:.3f},\"ticks\":{},\"spawnRate\":{},\"multiplier\":{},"
        "\"ballLimit\":{},\"elapsed\":{:.2f},\"physicsFps\":{:.1f},\"physicsFrameMs\":{:.3f},"
        "\"probe\":{{\"phase\":\"{}\",\"target\":{:.0f},\"fps\":{:.1f},\"best\":{}}},"
        "\"ballsByColor\":{{{}}},\"launchers\":[{}]}}",
        mode == GameMode::Normal ? "normal" : "performance", jsonEscape(files.name), diskNotice(),
        stateNames[static_cast<int>(view->state)], paused, backendKindName(view->backend),
        view->pendingBackend ? backendKindName(*view->pendingBackend) : std::string_view{},
        view->refilling, jsonEscape(view->backendFault), stats.ballCount, view->board->aliveBrickCount(),
        launchers.launcherCount(), view->pendingSpawn, stats.totalBallsSpawned,
        stats.totalHits, stats.lastFramePhysicsMs, view->deviceStepMs, stats.ticksLastFrame,
        spawnRateOptions[view->spawnRateIndex], ballMultiplierOptions[view->ballMultiplierIndex],
        view->ballLimit, view->elapsedSeconds, view->physicsFramesPerSecond, view->physicsFrameMs,
        probePhaseName(probe.phase()), probe.targetFps(),
        probe.lastFps(), probe.bestPassingCount(), ballsByColor, launcherColumns);
}

std::optional<ControlResponse> GameplayScreen::handleControl(const ControlRequest& request) {
    if (!runner) {
        return std::nullopt;
    }
    if (!view) {
        view = runner->view();
    }
    if (!view) {
        return ControlResponse::error(409, "the level is still loading");
    }
    const std::string& path = request.path;
    const ControlResponse done = ControlResponse::ok("{\"ok\":true}");
    if (path == "/launch") {
        const int column = static_cast<int>(request.integer("column").value_or(0));
        const int row = static_cast<int>(request.integer("row").value_or(0));
        if (!view->launchers.visible(column, row) || view->state == SessionState::Won) {
            return ControlResponse::error(409, "no launcher there or game not running");
        }
        runner->post([column, row](GameSession& session) { session.chooseLauncher(column, row); });
        return done;
    }
    if (path == "/start") {
        runner->post([](GameSession& session) { session.startAutoRelease(); });
        return done;
    }
    if (path == "/probe") {
        if (request.integer("cancel").value_or(0) != 0) {
            runner->post([](GameSession& session) { session.cancelCapacityProbe(); });
        } else {
            probeTargetFps = std::clamp(request.integer("fps").value_or(probeTargetFps),
                                        minimumProbeTargetFps, maximumProbeTargetFps);
            startProbe();
        }
        return done;
    }
    if (path == "/rate") {
        const int index = static_cast<int>(request.integer("index").value_or(view->spawnRateIndex));
        runner->post([index](GameSession& session) { session.selectSpawnRate(index); });
        return done;
    }
    if (path == "/fill") {
        const auto count = static_cast<BallCount>(request.count("count").value_or(0));
        runner->post([count](GameSession& session) { session.fillWithBalls(count); });
        return done;
    }
    if (path == "/limit") {
        const int index = static_cast<int>(request.integer("index").value_or(view->ballLimitIndex));
        runner->post([index](GameSession& session) {
            session.cancelCapacityProbe();
            session.selectBallLimit(index);
        });
        return done;
    }
    if (path == "/multiplier") {
        const int index = static_cast<int>(request.integer("index").value_or(view->ballMultiplierIndex));
        runner->post([index](GameSession& session) { session.selectBallMultiplier(index); });
        return done;
    }
    if (path == "/backend") {
        const auto kind = parseBackendKind(request.text("kind").value_or(""));
        if (!kind || !backendStatus(*kind).available) {
            return ControlResponse::error(409, "backend unknown or unavailable");
        }
        runner->post([kind = *kind](GameSession& session) { session.requestBackend(kind); });
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
    if (path == "/compute-gpu") {
        const auto index = request.integer("index");
        if (!index || *index < 0 || *index >= static_cast<long long>(app.computeAdapters().size())) {
            return ControlResponse::error(400, "index must name a Vulkan adapter");
        }
        selectComputeAdapter(static_cast<int>(*index));
        return done;
    }
    if (path == "/restart") {
        restartLevel();
        return done;
    }
    return std::nullopt;
}
