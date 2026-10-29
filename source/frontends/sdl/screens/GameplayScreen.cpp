#include "screens/GameplayScreen.h"

#include "app/App.h"
#include "app/LaunchOptions.h"
#include "app/Paths.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "render/BoardRenderer.h"
#include "render/LauncherRenderer.h"
#include "render/Palette.h"
#include "screens/DevicePicker.h"
#include "screens/SettingsPanel.h"
#include "ui/MenuModel.h"
#include "sim/BackendWorker.h"
#include "ui/Widgets.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>

namespace {

constexpr float controlsScrollStep = 40.0f;

constexpr float mainSectionCornerRadius = 0.0f;
constexpr float overlayWidth = 420.0f;
constexpr float overlayButtonHeight = 60.0f;
constexpr float controlRowHeight = 44.0f;
constexpr float controlGap = 10.0f;
constexpr DevicePickerStyle gameplayDevicePicker{controlRowHeight, controlGap, 0.3f, 18.0f};
constexpr float groupLabelHeight = 26.0f;
constexpr float groupGap = 14.0f;
constexpr float minimumControlColumnWidth = 220.0f;
constexpr float controlColumnGap = 16.0f;
constexpr float probeStatusHeight = 28.0f;
constexpr float advancedHeaderHeight = 40.0f;
constexpr float collisionsHintSize = 15.0f;
constexpr float collisionsHintHeight = 40.0f;
constexpr float collisionsGroupHeight = controlRowHeight + controlGap + collisionsHintHeight;
constexpr float probeStatusSize = 17.0f;

float listHeight(size_t rows) {
    return (controlRowHeight + controlGap) * static_cast<float>(rows) - controlGap;
}

constexpr double millisecondsPerSecond = 1000.0;
constexpr int minimumProbeTargetFps = 10;
constexpr int maximumProbeTargetFps = 1000;
constexpr int defaultTargetFps = 60;

} // namespace

GameplayScreen::GameplayScreen(App& app, GameMode mode, LevelFiles files)
    : app(app), mode(mode), files(std::move(files)), probeTargetFps(defaultTargetFps) {
    loadSession();
    refreshSettings();
}

void GameplayScreen::refreshSettings() {
    if (!runner) {
        return;
    }
    runner->setBallCollisions(app.settings().ballCollisions);
}

void GameplayScreen::loadSession() {
    auto level = loadLevel(files);
    if (!level) {
        runner.reset();
        view.reset();
        loadError = level.error();
        app.flow().levelFailed(loadError);
        return;
    }
    loadError.clear();
    view.reset();
    runner = std::make_unique<SessionRunner>(mode, std::move(*level), app.preferredBackend());
    applyDefaultBallLimit();
    app.flow().levelLoaded();
}

void GameplayScreen::applyDefaultBallLimit() {
    if (mode == GameMode::Performance) {
        useAutoBallLimit();
    }
}

void GameplayScreen::useAutoBallLimit() {
    runner->post([](GameSession& session) { session.useCapacityBallLimit(); });
}

void GameplayScreen::handleSessionEvents() {
    for (const SessionEvent& event : runner->takeEvents()) {
        if (event.kind == SessionEventKind::BallCollisionsChanged) {
            app.showToast(std::string(ballCollisionsMessage(event.collisionsChange)), false);
        } else if (event.kind == SessionEventKind::BackendSwitched) {
            applyDefaultBallLimit();
        } else {
            app.showToast(backendFailedMessage(event.backend), true);
        }
    }
}

void GameplayScreen::startProbe() {
    const double targetFps = static_cast<double>(probeTargetFps);
    runner->post([targetFps](GameSession& session) { session.startCapacityProbe(targetFps); });
}

LevelDiskState GameplayScreen::diskState() const {
    return app.flow().diskState();
}

bool GameplayScreen::isPaused() const {
    return app.flow().isPaused();
}

void GameplayScreen::setPaused(bool pause) {
    if (pause && !app.flow().isPaused()) {
        app.flow().activate(FlowAction::TogglePause);
    } else if (!pause && app.flow().isPaused()) {
        if (app.flow().overlay() == FlowOverlay::Settings) {
            app.flow().activate(FlowAction::Back);
        }
        app.flow().activate(FlowAction::Resume);
    }
}

void GameplayScreen::restart() {
    if (app.flow().overlay() == FlowOverlay::None) {
        app.flow().activate(FlowAction::TogglePause);
    }
    app.flow().activate(FlowAction::Restart);
}

void GameplayScreen::restartSession() {
    if (runner) {
        runner->post([](GameSession& session) { session.restart(); });
    }
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

void GameplayScreen::update(float /*deltaTime*/) {
    if (!runner) {
        return;
    }
    view = runner->view();
    handleSessionEvents();
    if (!view) {
        runner->tick({app.flow().isPaused(), 0.0, 0.0, 0.0f});
        return;
    }
    app.flow().sessionStateChanged(view->state);
    const GameplayLayout layout = computeGameplayLayout(
        {static_cast<float>(gfx::screenWidth()), static_cast<float>(gfx::screenHeight())}, view->board->columns(),
        view->board->rows(), app.settings().layout, mode == GameMode::Normal ? view->launchers.visibleRows() : 0);
    if (!app.flow().isPaused()) {
        handleLauncherClick(layout);
    }
    tickPhysics(layout);
}

// The physics thread keeps its own pace; this hands it the pause state, the UI's frame timings for the capacity probe
// and the visibility slot size. The view drawn this frame was taken above, so commands posted while drawing show
// from a later view.
void GameplayScreen::tickPhysics(const GameplayLayout& layout) {
    const FrameSample& lastFrame = app.frameTimings().latest();
    PhysicsFrameInput input;
    input.paused = app.flow().isPaused();
    input.uiFrameSeconds = lastFrame.workMs / millisecondsPerSecond;
    input.uiWorkSeconds = std::max(lastFrame.cpuMs, lastFrame.gpuMs) / millisecondsPerSecond;
    input.visibilitySlotCells = BallView::slotCells(layout.board, view->ballRadius);
    runner->tick(input);
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
        ui::drawTextInRect(app.flow().isPaused() ? "Resume" : "Pause", header, 22.0f, palette::textStrong);
    } else {
        const std::string title = app.flow().gameTitle();
        const std::string_view notice = app.flow().diskNotice();
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
        app.flow().activate(FlowAction::TogglePause);
    }
}

void GameplayScreen::drawMainSection(const GameplayLayout& layout) {
    gfx::sceneRect(layout.mainSection, palette::boardBackdrop, mainSectionCornerRadius);
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

} // namespace

void GameplayScreen::selectComputeAdapter(int adapter) {
    setComputeGpuPreference(std::to_string(adapter));
    refreshBackendStatuses();
    if (view->backend == BackendKind::Vulkan || view->pendingBackend == BackendKind::Vulkan) {
        runner->post([](GameSession& session) { session.requestBackend(BackendKind::Vulkan); });
        app.showToast(computeGpuMessage(app.computeAdapters()[static_cast<size_t>(adapter)].name), false);
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

// The backend the physics list highlights: the one starting, else the one running.
BackendKind GameplayScreen::shownBackend() const { return view->pendingBackend.value_or(view->backend); }

// Changing a device's backend switches the running game to the kind the picks make (Hybrid, or one backend).
void GameplayScreen::drawDevicePicker(Rect area) {
    if (const auto changed = ::drawDevicePicker(area, displayedPicks(shownBackend()), gameplayDevicePicker)) {
        setBackendPicks(*changed);
        refreshBackendStatuses();
        const BackendKind kind = kindForPicks(*changed);
        runner->post([kind](GameSession& session) { session.requestBackend(kind); });
        app.showToast(physicsOnMessage(*changed), false);
    }
}

void GameplayScreen::drawBackendList(Rect area, BackendKind selected) {
    float cursor = area.y;
    for (const BackendRow& rowInfo : gameplayBackendRows(*view)) {
        const BackendKind kind = rowInfo.kind;
        const bool available = rowInfo.option.enabled;
        const bool starting = view->pendingBackend == kind;
        const bool pressed = ui::button({area.x, cursor, area.width, controlRowHeight}, rowInfo.option.label, available,
                                        starting || (selected == kind && !view->pendingBackend));
        if (pressed && available) {
            runner->post([kind](GameSession& session) { session.requestBackend(kind); });
        }
        cursor += controlRowHeight + controlGap;
    }
}

// Off / On, saved with the settings like before; collisions need the Scalar backend, so On is disabled where Scalar
// is not offered.
void GameplayScreen::drawBallCollisions(Rect area) {
    const bool enabled = app.settings().ballCollisions;
    const BallCollisionsChoice choice = ballCollisionsChoice(enabled);
    const bool possible = choice.options[1].enabled;
    const Rect buttons{area.x, area.y, area.width, controlRowHeight};
    bool wanted = enabled;
    if (ui::button(ui::columnSlice(buttons, 0, 2, controlGap), "Off", true, !enabled)) {
        wanted = false;
    }
    if (ui::button(ui::columnSlice(buttons, 1, 2, controlGap), "On", possible, enabled)) {
        wanted = true;
    }
    const float hintY = area.y + controlRowHeight + controlGap;
    const float lineHeight = collisionsHintHeight * 0.5f;
    ui::drawTextInRect(choice.hint, {area.x, hintY, area.width, lineHeight * 2.0f},
                       collisionsHintSize, palette::textMuted, ui::Align::Left);
    if (wanted != enabled) {
        Settings next = app.settings();
        next.ballCollisions = wanted;
        app.applySettings(next);
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

    const UiOption startOption = releaseButton(*view);
    const std::string startLabel = startOption.label;
    if (ui::button(group("Release", controlRowHeight), startLabel, startOption.enabled)) {
        runner->post([](GameSession& session) { session.startAutoRelease(); });
    }

    const auto spawnChoices = spawnRateChoices(view->spawnRateIndex);
    std::vector<std::string> spawnLabels;
    for (const UiOption& option : spawnChoices) {
        spawnLabels.push_back(option.label);
    }
    const float spawnHeight =
        controlRowHeight + (spawnDropdownOpen ? (controlRowHeight + controlGap) * spawnLabels.size() : 0.0f);
    const int pickedSpawn =
        drawOptionDropdown(group("Spawn / sec", spawnHeight), spawnLabels[view->spawnRateIndex],
                           spawnLabels, view->spawnRateIndex, spawnDropdownOpen);
    if (pickedSpawn >= 0) {
        runner->post([pickedSpawn](GameSession& session) { session.selectSpawnRate(pickedSpawn); });
    }

    const auto countChoices = ballMultiplierChoices(view->ballMultiplierIndex);
    std::vector<std::string> countLabels;
    for (const UiOption& option : countChoices) {
        countLabels.push_back(option.label);
    }
    const float countHeight =
        controlRowHeight + (countDropdownOpen ? (controlRowHeight + controlGap) * countLabels.size() : 0.0f);
    const int pickedCount =
        drawOptionDropdown(group("Ball count", countHeight), countLabels[view->ballMultiplierIndex],
                           countLabels, view->ballMultiplierIndex, countDropdownOpen);
    if (pickedCount >= 0) {
        runner->post([pickedCount](GameSession& session) { session.selectBallMultiplier(pickedCount); });
    }

    const auto limitChoices = ballLimitChoices(*view);
    std::vector<std::string> limitLabels;
    for (const UiOption& option : limitChoices) {
        limitLabels.push_back(option.label);
    }
    const bool autoLimit = isAutoBallLimit(*view);
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
        ui::drawTextInRect(probeStatusText(*view),
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
        const float backendHeight = (controlRowHeight + controlGap) * selectableBackends().size() - controlGap;
        drawBackendList(group("Physics", backendHeight), view->backend);
        if (showsDevicePicks(shownBackend())) {
            drawDevicePicker(group("Devices", devicePickerHeight(gameplayDevicePicker)));
        }
        if (const std::vector<int> choices = renderAdapterChoices(); !choices.empty()) {
            drawAdapterList(group("Render GPU", listHeight(choices.size())), app.renderAdapters(), choices,
                            app.renderAdapterIndex(), false);
        }
        if (const std::vector<int> choices = computeAdapterChoices(); !choices.empty()) {
            drawAdapterList(group("Compute GPU (Vulkan backend)", listHeight(choices.size())), app.computeAdapters(),
                            choices, selectGpuAdapter(app.computeAdapters(), computeGpuPreference(), GpuRole::Compute),
                            true);
        }
        // Last, across both columns: a testing aid outside the game rules.
        const float collisionsY = std::max(columnCursors[0], columnCursors[1]);
        ui::drawText("Ball collisions", {inner.x, collisionsY}, 20.0f, palette::textMuted);
        drawBallCollisions({inner.x, collisionsY + groupLabelHeight, contentWidth, collisionsGroupHeight});
        const float collisionsEnd = collisionsY + groupLabelHeight + collisionsGroupHeight + groupGap;
        columnCursors = {collisionsEnd, collisionsEnd};
    }
    performanceControlsHeight = std::max(columnCursors[0], columnCursors[1]) - top - groupGap;
    ui::scrollHints(inner, controlsScroll, performanceControlsHeight, palette::panel);
    gfx::endClip();
}

float GameplayScreen::normalContentHeight() const {
    const float launcherHeight = 320.0f;
    const float advancedHeight = app.settings().advancedSettings
                                     ? groupGap + groupLabelHeight +
                                           (controlRowHeight + controlGap) * selectableBackends().size() - controlGap +
                                           (showsDevicePicks(shownBackend())
                                                ? groupGap + groupLabelHeight + devicePickerHeight(gameplayDevicePicker)
                                                : 0.0f) +
                                           groupGap + groupLabelHeight + collisionsGroupHeight
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
                          !app.flow().isPaused() && view->state == SessionState::Running);
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
    drawLaunchers(view->launchers, grid, !app.flow().isPaused() && view->state == SessionState::Running);
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
        const float backendHeight = (controlRowHeight + controlGap) * selectableBackends().size() - controlGap;
        drawBackendList({inner.x, cursor, width, backendHeight}, view->backend);
        cursor += backendHeight + groupGap;
        ui::drawText("Ball collisions", {inner.x, cursor}, 20.0f, palette::textMuted);
        cursor += groupLabelHeight;
        drawBallCollisions({inner.x, cursor, width, collisionsGroupHeight});
        cursor += collisionsGroupHeight + groupGap;
        if (showsDevicePicks(shownBackend())) {
            ui::drawText(std::string(menuText::devicesLabel), {inner.x, cursor}, 20.0f, palette::textMuted);
            cursor += groupLabelHeight;
            drawDevicePicker({inner.x, cursor, width, devicePickerHeight(gameplayDevicePicker)});
        }
    }
    ui::scrollHints(inner, normalScroll, normalContentHeight(), palette::panel);
    gfx::endClip();
}

void GameplayScreen::drawOverlay() {
    const FlowOverlay overlay = app.flow().overlay();
    if (overlay == FlowOverlay::None) {
        return;
    }
    const float width = static_cast<float>(gfx::screenWidth());
    const float height = static_cast<float>(gfx::screenHeight());
    gfx::fillRect({0.0f, 0.0f, width, height}, gfx::fade(palette::boardBackdrop, 0.7f));
    if (overlay == FlowOverlay::Settings) {
        const Rect panel{(width - settingsPanelWidth) * 0.5f, (height - settingsPanelHeight) * 0.5f,
                         settingsPanelWidth, settingsPanelHeight};
        ui::drawPanel(panel, palette::panel, 0.08f);
        const SettingsPanelEdit edit =
            drawSettingsPanel(panel, app.settings(), app.preferredBackend(), app.benchmarkScores(),
                              app.isWebGpuSupported(), pauseSettingsScroll, app.flow().canRunMachineBenchmark());
        if (edit.changed) {
            app.applySettings(edit.settings, edit.backend);
        }
        if (edit.runBenchmark) {
            app.flow().activate(FlowAction::RunMachineBenchmark);
        }
        if (edit.backPressed) {
            app.flow().activate(FlowAction::Back);
        }
        return;
    }
    const auto buttons = app.flow().buttons();
    const float panelHeight = 120.0f + static_cast<float>(buttons.size()) * (overlayButtonHeight + 14.0f);
    const Rect panel{(width - overlayWidth) * 0.5f, (height - panelHeight) * 0.5f, overlayWidth, panelHeight};
    ui::drawPanel(panel, palette::panel, 0.08f);
    ui::drawText(app.flow().title(), {panel.x + panel.width * 0.5f, panel.y + 28.0f}, 44.0f,
                 overlay == FlowOverlay::Win ? palette::accent : palette::text, ui::Align::Center);
    auto overlayButton = [&](int index) {
        return Rect{panel.x + 30.0f,
                    panel.y + 100.0f + static_cast<float>(index) * (overlayButtonHeight + 14.0f),
                    panel.width - 60.0f, overlayButtonHeight};
    };
    for (size_t index = 0; index < buttons.size(); ++index) {
        if (ui::button(overlayButton(static_cast<int>(index)), buttons[index].label, buttons[index].enabled)) {
            app.flow().activate(buttons[index].action);
        }
    }
}

void GameplayScreen::draw() {
    if (!runner) {
        const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
        ui::drawText(app.flow().title(), {centerX, 200.0f}, 40.0f, palette::danger, ui::Align::Center);
        ui::drawText(app.flow().loadError(), {centerX, 260.0f}, 22.0f, palette::text, ui::Align::Center);
        ui::drawText(app.flow().footnote(), {centerX, 300.0f}, 22.0f, palette::textMuted, ui::Align::Center);
        for (const FlowButton& button : app.flow().buttons()) {
            if (ui::button({centerX - 120.0f, 360.0f, 240.0f, 56.0f}, button.label, button.enabled)) {
                app.flow().activate(button.action);
            }
        }
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
        drawDebugPanel(layout.debugSection, *view, app.frameTimings(), debugState, app.settings().advancedSettings, {}, app.rendererDescription());
    }
    drawOverlay();
}

void GameplayScreen::post(std::function<void(GameSession&)> command) {
    if (runner) {
        runner->post(std::move(command));
    }
}

void GameplayScreen::startCapacityProbe(int targetFps) {
    probeTargetFps = targetFps;
    startProbe();
}

// The shared gameplay routes are the core's; the compute GPU list is SDL's own.
std::optional<ControlResponse> GameplayScreen::handleControl(const ControlRequest& request) {
    if (request.path != "/compute-gpu" || !runner) {
        return std::nullopt;
    }
    const auto index = request.integer("index");
    if (!index || *index < 0 || *index >= static_cast<long long>(app.computeAdapters().size())) {
        return ControlResponse::error(400, "index must name a Vulkan adapter");
    }
    selectComputeAdapter(static_cast<int>(*index));
    return ControlResponse::ok();
}
