#include "editor/MapEditorScreen.h"

#include "app/App.h"
#include "app/Paths.h"
#include "editor/LevelEditor.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"
#include "render/Palette.h"
#include "ui/GameplayLayout.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <format>

namespace {

constexpr float margin = 16.0f;
constexpr float sidePanelWidth = 270.0f;
constexpr float propertyPanelWidth = 330.0f;
constexpr float brickAreaShare = 0.66f;
constexpr float minimumGridWidth = 320.0f;
constexpr float portraitGridShare = 0.55f;
constexpr float panelTabsHeight = 40.0f;
constexpr float controlHeight = 38.0f;
constexpr float controlGap = 8.0f;


Rect nextControl(Rect area, float& cursor, float height = controlHeight) {
    const Rect rect{area.x + 12.0f, cursor, area.width - 24.0f, height};
    cursor += height + controlGap;
    return rect;
}

void sectionTitle(Rect area, float& cursor, std::string_view text) {
    cursor += 6.0f;
    ui::drawText(text, {area.x + 12.0f, cursor}, 22.0f, palette::accent);
    cursor += 30.0f;
}

}

MapEditorScreen::MapEditorScreen(App& app, GameMode mode, std::optional<LevelFiles> files)
    : app(app), session(mode, files, ::levelDirectory(GameMode::Normal), ::levelDirectory(GameMode::Performance)) {}

MapEditorScreen::Regions MapEditorScreen::computeRegions() const {
    const float width = static_cast<float>(gfx::screenWidth());
    const float height = static_cast<float>(gfx::screenHeight());
    Regions regions;
    const float landscapeGridWidth = width - sidePanelWidth - propertyPanelWidth - margin * 4.0f;
    regions.isPortrait = resolveOrientation(app.settings().layout, width, height) == LayoutOrientation::Portrait ||
                         landscapeGridWidth < minimumGridWidth;
    if (regions.isPortrait) {
        const float contentWidth = width - margin * 2.0f;
        const float gridsHeight = (height - margin * 4.0f - panelTabsHeight) * portraitGridShare;
        const float brickHeight = (gridsHeight - margin) * brickAreaShare;
        regions.brickArea = {margin, margin, contentWidth, brickHeight};
        regions.launcherArea = {margin, margin * 2.0f + brickHeight, contentWidth, gridsHeight - margin - brickHeight};
        regions.panelTabs = {margin, margin * 2.0f + gridsHeight, contentWidth, panelTabsHeight};
        const float panelTop = regions.panelTabs.y + panelTabsHeight + controlGap;
        regions.tools = {margin, panelTop, contentWidth, height - margin - panelTop};
        regions.properties = regions.tools;
        return regions;
    }
    regions.tools = {margin, margin, sidePanelWidth, height - margin * 2.0f};
    regions.properties = {width - propertyPanelWidth - margin, margin, propertyPanelWidth, height - margin * 2.0f};
    const float centerX = regions.tools.x + regions.tools.width + margin;
    const float centerWidth = regions.properties.x - margin - centerX;
    const float brickHeight = (height - margin * 3.0f) * brickAreaShare;
    regions.brickArea = {centerX, margin, centerWidth, brickHeight};
    regions.launcherArea = {centerX, margin * 2.0f + brickHeight, centerWidth, height - margin * 3.0f - brickHeight};
    return regions;
}

float MapEditorScreen::brickCellSize(Rect area) const {
    const BrickLayout& bricks = session.level().bricks;
    const Rect inner = ui::shrink(area, 14.0f);
    return std::max(2.0f, std::min(inner.width / static_cast<float>(std::max(bricks.width, 1)),
                                   inner.height / static_cast<float>(std::max(bricks.playableHeight, 1))));
}

Rect MapEditorScreen::brickGridRect(Rect area) const {
    const BrickLayout& bricks = session.level().bricks;
    const float cell = brickCellSize(area);
    const float gridWidth = cell * static_cast<float>(bricks.width);
    const float gridHeight = cell * static_cast<float>(bricks.playableHeight);
    return {area.x + (area.width - gridWidth) * 0.5f, area.y + (area.height - gridHeight) * 0.5f, gridWidth, gridHeight};
}

float MapEditorScreen::launcherCellSize(Rect area) const {
    const LauncherLayout& launchers = session.level().launchers;
    const Rect inner = ui::shrink(area, 14.0f);
    const float rows = static_cast<float>(launchers.rowCount() + 1);
    const float columns = static_cast<float>(std::max(launchers.columns, 1));
    return std::max(14.0f, std::min({inner.width / columns, inner.height / rows, 64.0f}));
}

Rect MapEditorScreen::launcherGridRect(Rect area) const {
    const LauncherLayout& launchers = session.level().launchers;
    const float cell = launcherCellSize(area);
    const float gridWidth = cell * static_cast<float>(launchers.columns);
    const float gridHeight = cell * static_cast<float>(launchers.rowCount() + 1);
    return {area.x + (area.width - gridWidth) * 0.5f, area.y + 14.0f, gridWidth, gridHeight};
}

MapEditorScreen::GridCell MapEditorScreen::brickCellAt(Rect area, Vec2 point) const {
    const Rect grid = brickGridRect(area);
    const BrickLayout& bricks = session.level().bricks;
    if (!pointInRect(point, grid)) {
        return {};
    }
    const float cell = brickCellSize(area);
    const int column = static_cast<int>((point.x - grid.x) / cell);
    const int row = static_cast<int>((point.y - grid.y) / cell);
    if (column >= bricks.width || row >= bricks.height) {
        return {};
    }
    return {column, row};
}

MapEditorScreen::GridCell MapEditorScreen::launcherCellAt(Rect area, Vec2 point) const {
    const Rect grid = launcherGridRect(area);
    if (!pointInRect(point, grid)) {
        return {};
    }
    const float cell = launcherCellSize(area);
    return {static_cast<int>((point.x - grid.x) / cell), static_cast<int>((point.y - grid.y) / cell)};
}

void MapEditorScreen::selectColor(BallColor color) {
    session.selectColor(color);
}

void MapEditorScreen::changeSelectedHits(int hits) {
    session.setBrushHits(hits);
}

bool MapEditorScreen::save() {
    return session.save();
}

void MapEditorScreen::handleShortcuts() {
    if (nameFocused) {
        return;
    }
    const bool control = input::keyDown(SDL_SCANCODE_LCTRL) || input::keyDown(SDL_SCANCODE_RCTRL);
    if (control && input::keyPressed(SDL_SCANCODE_Z)) {
        session.handleShortcut(EditorShortcut::Undo);
    }
    if (control && input::keyPressed(SDL_SCANCODE_Y)) {
        session.handleShortcut(EditorShortcut::Redo);
    }
    if (control && input::keyPressed(SDL_SCANCODE_S)) {
        session.handleShortcut(EditorShortcut::Save);
    }
    if (control) {
        return;
    }
    for (int i = 0; i < ballColorCount; ++i) {
        if (input::keyPressed(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i))) {
            session.handleShortcut(static_cast<EditorShortcut>(static_cast<int>(EditorShortcut::Color1) + i));
        }
    }
    if (input::keyPressed(SDL_SCANCODE_P)) {
        session.handleShortcut(EditorShortcut::Paint);
    }
    if (input::keyPressed(SDL_SCANCODE_E)) {
        session.handleShortcut(EditorShortcut::Extend);
    }
    if (input::keyPressed(SDL_SCANCODE_X)) {
        session.handleShortcut(EditorShortcut::Erase);
    }
    if (input::keyPressed(SDL_SCANCODE_K)) {
        session.handleShortcut(EditorShortcut::Pick);
    }
}

void MapEditorScreen::handleBrickInput(Rect area) {
    const bool leftDown = input::mouseDown(input::MouseButton::Left);
    const bool rightDown = input::mouseDown(input::MouseButton::Right);
    if (!leftDown && !rightDown) {
        if (strokeInProgress) {
            session.endStroke();
            strokeInProgress = false;
        }
        return;
    }
    const GridCell cell = brickCellAt(area, input::mousePosition());
    const bool pressed = input::mousePressed(input::MouseButton::Left) || input::mousePressed(input::MouseButton::Right);
    if (pressed && cell.isValid()) {
        session.beginStroke(rightDown);
        strokeInProgress = true;
    }
    if (!strokeInProgress || !cell.isValid()) {
        return;
    }
    session.strokeCell(cell.column, cell.row);
}

void MapEditorScreen::handleLauncherInput(Rect area) {
    const bool leftPressed = input::mousePressed(input::MouseButton::Left);
    const bool rightPressed = input::mousePressed(input::MouseButton::Right);
    if (!leftPressed && !rightPressed) {
        return;
    }
    const GridCell cell = launcherCellAt(area, input::mousePosition());
    if (!cell.isValid()) {
        return;
    }
    const bool removeRow = rightPressed && input::keyDown(SDL_SCANCODE_LSHIFT);
    if (removeRow) {
        session.clickLauncher(cell.column, cell.row, LauncherClick::DeleteRow);
        return;
    }
    if (rightPressed) {
        session.clickLauncher(cell.column, cell.row, LauncherClick::Clear);
        return;
    }
    session.clickLauncher(cell.column, cell.row, LauncherClick::Set);
}

void MapEditorScreen::update(float deltaTime) {
    session.update(deltaTime);
    if (input::keyPressed(SDL_SCANCODE_ESCAPE)) {
        if (nameFocused) {
            nameFocused = false;
        } else {
            if (session.back() == EditorLeave::Leave) {
                app.flow().activate(FlowAction::Back);
            }
        }
        return;
    }
    handleShortcuts();
    const Regions regions = computeRegions();
    handleBrickInput(regions.brickArea);
    handleLauncherInput(regions.launcherArea);
}

void MapEditorScreen::drawToolPanel(Rect area) {
    ui::drawPanel(area, palette::panel, 0.03f);
    float visibleHeight = 0.0f;
    ui::scrollFrame(area, toolsScroll, toolsContentHeight, visibleHeight);
    gfx::beginClip(area);
    const float contentTop = area.y - toolsScroll;
    float cursor = contentTop + 8.0f;
    sectionTitle(area, cursor, "Tool");
    for (const EditorToolButton& button : EditorSession::toolButtons()) {
        if (ui::button(nextControl(area, cursor), std::string(button.label), true, session.tool() == button.tool)) {
            session.setTool(button.tool);
        }
    }

    sectionTitle(area, cursor, "Color (1-7)");
    const Rect swatchRow = nextControl(area, cursor, 34.0f);
    const Rect swatchRow2 = nextControl(area, cursor, 34.0f);
    for (int i = 0; i < ballColorCount; ++i) {
        const Rect row = i < 4 ? swatchRow : swatchRow2;
        const Rect swatch = ui::columnSlice(row, i % 4, 4, 8.0f);
        const BallColor color = ballColorFromIndex(i);
        gfx::fillRect(swatch, palette::ballColor(color));
        if (color == session.brushColor()) {
            gfx::strokeRect(ui::shrink(swatch, -3.0f), 2.0f, palette::textStrong);
        }
        if (ui::isClicked(swatch)) {
            session.selectColor(color);
        }
    }

    sectionTitle(area, cursor, "Brick");
    int hits = session.brushHits();
    if (ui::stepper(nextControl(area, cursor), "Hits", hits, 1, editorMaximumHits)) {
        session.setBrushHits(hits);
    }
    ui::drawTextInRect(session.selectionText(), nextControl(area, cursor, 24.0f), 18.0f, palette::textMuted, ui::Align::Left);

    sectionTitle(area, cursor, "Launcher");
    int balls = session.launcherBalls();
    if (ui::stepper(nextControl(area, cursor), "Balls", balls, 1, editorMaximumLauncherBalls)) {
        session.setLauncherBalls(balls);
    }
    ui::drawTextInRect("L-click set, R-click clear", nextControl(area, cursor, 22.0f), 16.0f, palette::textMuted,
                       ui::Align::Left);
    ui::drawTextInRect("Shift+R-click delete row", nextControl(area, cursor, 22.0f), 16.0f, palette::textMuted,
                       ui::Align::Left);
    toolsContentHeight = cursor - contentTop;
    ui::scrollHints(area, toolsScroll, toolsContentHeight, palette::panel);
    gfx::endClip();
}

void MapEditorScreen::drawPropertyPanel(Rect area) {
    ui::drawPanel(area, palette::panel, 0.03f);
    float visibleHeight = 0.0f;
    ui::scrollFrame(area, propertiesScroll, propertiesContentHeight, visibleHeight);
    gfx::beginClip(area);
    const float contentTop = area.y - propertiesScroll;
    float cursor = contentTop + 8.0f;
    sectionTitle(area, cursor, "Level");
    std::string name = session.name();
    if (ui::textField(nextControl(area, cursor), name, nameFocused, editorMaximumNameLength)) {
        session.setName(name);
    }
    const Rect modeRow = nextControl(area, cursor);
    if (ui::button(ui::columnSlice(modeRow, 0, 2, 8.0f), "Normal", true, session.mode() == GameMode::Normal)) {
        session.setMode(GameMode::Normal);
    }
    if (ui::button(ui::columnSlice(modeRow, 1, 2, 8.0f), "Perf", true, session.mode() == GameMode::Performance)) {
        session.setMode(GameMode::Performance);
    }

    sectionTitle(area, cursor, "Board");
    const BrickLayout& bricks = session.level().bricks;
    int width = bricks.width;
    int height = bricks.height;
    int playable = bricks.playableHeight;
    const bool widthChanged = ui::stepper(nextControl(area, cursor), "Width", width, 1, editorMaximumBoardSize);
    const bool heightChanged = ui::stepper(nextControl(area, cursor), "Brick rows", height, 1, editorMaximumBoardSize);
    const bool playableChanged =
        ui::stepper(nextControl(area, cursor), "Total rows", playable, height, editorMaximumTotalRows);
    if (widthChanged || heightChanged || playableChanged) {
        session.setBoardSize(width, height, playable);
    }

    sectionTitle(area, cursor, "Launchers");
    int columns = session.level().launchers.columns;
    int visibleRows = session.level().launchers.visibleRows;
    if (ui::stepper(nextControl(area, cursor), "Columns", columns, 1, editorMaximumLauncherColumns)) {
        session.setLauncherColumns(columns);
    }
    if (ui::stepper(nextControl(area, cursor), "Visible rows", visibleRows, 1, editorMaximumVisibleRows)) {
        session.setVisibleRows(visibleRows);
    }
    if (ui::button(nextControl(area, cursor), "Auto-fill to match bricks")) {
        session.autoFillLaunchers();
    }

    sectionTitle(area, cursor, "Validation");
    const EditorValidation validation = session.validation();
    if (validation.isPlayable()) {
        ui::drawText("Ready to play", {area.x + 12.0f, cursor}, 20.0f, palette::success);
        cursor += 26.0f;
    }
    for (const std::string& problem : validation.problems) {
        ui::drawText(problem, {area.x + 12.0f, cursor}, 17.0f, palette::danger);
        cursor += 22.0f;
    }

    const float buttonsTop = contentTop + area.height - (controlHeight + controlGap) * 4.0f - 8.0f;
    cursor = std::max(cursor + 8.0f, buttonsTop);
    const Rect undoRow = nextControl(area, cursor);
    if (ui::button(ui::columnSlice(undoRow, 0, 2, 8.0f), "Undo", session.canUndo())) {
        session.undo();
    }
    if (ui::button(ui::columnSlice(undoRow, 1, 2, 8.0f), "Redo", session.canRedo())) {
        session.redo();
    }
    if (ui::button(nextControl(area, cursor), std::string(session.saveLabel()))) {
        save();
    }
    if (ui::button(nextControl(area, cursor), "Save & Play", validation.isPlayable())) {
        if (auto files = session.saveAndPlay()) {
            app.flow().playFromEditor(session.mode(), *files);
        }
    }
    if (ui::button(nextControl(area, cursor), std::string(session.backLabel()))) {
        if (session.back() == EditorLeave::Leave) {
            app.flow().activate(FlowAction::Back);
        }
    }
    propertiesContentHeight = cursor - contentTop;
    ui::scrollHints(area, propertiesScroll, propertiesContentHeight, palette::panel);
    gfx::endClip();
}

void MapEditorScreen::drawPanelTabs(Rect area) {
    if (ui::button(ui::columnSlice(area, 0, 2, controlGap), "Tools", true, portraitPanel == PortraitPanel::Tools)) {
        portraitPanel = PortraitPanel::Tools;
    }
    if (ui::button(ui::columnSlice(area, 1, 2, controlGap), "Level", true, portraitPanel == PortraitPanel::Level)) {
        portraitPanel = PortraitPanel::Level;
    }
}

void MapEditorScreen::drawBrickGrid(Rect area) {
    ui::drawPanel(area, palette::boardBackdrop, 0.02f);
    const BrickLayout& bricks = session.level().bricks;
    const Rect grid = brickGridRect(area);
    const float cell = brickCellSize(area);
    gfx::fillRect(grid, palette::shade(palette::boardBackdrop, 0.7f));
    gfx::fillRect({grid.x, grid.y, grid.width, cell * static_cast<float>(bricks.height)}, palette::panel);
    const GridCell hovered = brickCellAt(area, input::mousePosition());
    for (int row = 0; row < bricks.height; ++row) {
        for (int column = 0; column < bricks.width; ++column) {
            const int id = bricks.cellId(column, row);
            const Rect rect{grid.x + static_cast<float>(column) * cell, grid.y + static_cast<float>(row) * cell, cell,
                                 cell};
            const BrickType* type = id == emptyBrickId ? nullptr : bricks.findType(id);
            if (type) {
                Color fill = palette::brickColor(type->color);
                if (id == session.selectedBrick()) {
                    fill = palette::shade(fill, 1.35f);
                }
                gfx::fillRect(rect, fill);
                const float border = std::max(1.0f, cell * 0.08f);
                const Color edge = palette::shade(fill, 0.45f);
                if (column == 0 || bricks.cellId(column - 1, row) != id) {
                    gfx::fillRect({rect.x, rect.y, border, rect.height}, edge);
                }
                if (column == bricks.width - 1 || bricks.cellId(column + 1, row) != id) {
                    gfx::fillRect({rect.x + rect.width - border, rect.y, border, rect.height}, edge);
                }
                if (row == 0 || bricks.cellId(column, row - 1) != id) {
                    gfx::fillRect({rect.x, rect.y, rect.width, border}, edge);
                }
                if (row == bricks.height - 1 || bricks.cellId(column, row + 1) != id) {
                    gfx::fillRect({rect.x, rect.y + rect.height - border, rect.width, border}, edge);
                }
                const bool firstCellOfBrick = (column == 0 || bricks.cellId(column - 1, row) != id) &&
                                              (row == 0 || bricks.cellId(column, row - 1) != id);
                if (type->hits > 1 && firstCellOfBrick && cell >= 12.0f) {
                    ui::drawTextInRect(std::to_string(type->hits), rect, cell * 0.55f, palette::textStrong);
                }
            } else {
                gfx::strokeRect(rect, 1.0f, palette::border);
            }
            if (hovered.column == column && hovered.row == row) {
                gfx::strokeRect(rect, 2.0f, palette::accent);
            }
        }
    }
    const std::string size = std::format("{} x {}", bricks.width, bricks.height);
    const std::string footer = session.footerText();
    const bool footerFits = ui::measureText(footer, 18.0f).x <= area.width - 20.0f;
    ui::drawText(footerFits ? footer : size, {area.x + 10.0f, area.y + area.height - 26.0f}, 18.0f,
                 palette::textMuted);
}

void MapEditorScreen::drawLauncherGrid(Rect area) {
    ui::drawPanel(area, palette::panel, 0.04f);
    const LauncherLayout& launchers = session.level().launchers;
    const Rect grid = launcherGridRect(area);
    const float cell = launcherCellSize(area);
    const GridCell hovered = launcherCellAt(area, input::mousePosition());
    for (int row = 0; row <= launchers.rowCount(); ++row) {
        for (int column = 0; column < launchers.columns; ++column) {
            const Rect rect = ui::shrink(
                {grid.x + static_cast<float>(column) * cell, grid.y + static_cast<float>(row) * cell, cell, cell}, 3.0f);
            const bool exists = row < launchers.rowCount();
            const auto& slot = exists ? launchers.slot(column, row) : std::optional<Launcher>{};
            if (slot) {
                gfx::fillRect(rect, palette::ballColor(slot->color));
                ui::drawTextInRect(std::to_string(slot->ballCount), rect, std::max(10.0f, cell * 0.36f), palette::textStrong);
            } else {
                gfx::strokeRect(rect, 1.0f, exists ? palette::textDim : palette::border);
            }
            if (row == launchers.visibleRows - 1) {
                gfx::drawLine({grid.x, rect.y + rect.height + 4.0f}, {grid.x + grid.width, rect.y + rect.height + 4.0f}, 2.0f,
                           gfx::fade(palette::accent, 0.6f));
            }
            if (hovered.column == column && hovered.row == row) {
                gfx::strokeRect(rect, 2.0f, palette::accent);
            }
        }
    }
    const EditorValidation validation = session.validation();
    float x = area.x + 12.0f;
    const float y = area.y + area.height - 30.0f;
    for (int color = 0; color < ballColorCount; ++color) {
        if (validation.brickHits[color] == 0 && validation.balls[color] == 0) {
            continue;
        }
        const BallColor ballColor = ballColorFromIndex(color);
        gfx::fillCircle({x + 7.0f, y + 10.0f}, 7.0f, palette::ballColor(color));
        const std::string text = session.colorTallyText(ballColor);
        ui::drawText(text, {x + 18.0f, y}, 18.0f,
                     session.colorTallyMatches(ballColor) ? palette::success : palette::text);
        x += 30.0f + ui::measureText(text, 18.0f).x;
    }
}

void MapEditorScreen::drawMessage() {
    const EditorMessage& msg = session.message();
    if (msg.secondsLeft <= 0.0f) {
        return;
    }
    const Vec2 extent = ui::measureText(msg.text, 22.0f);
    const Rect rect{(static_cast<float>(gfx::screenWidth()) - extent.x) * 0.5f - 18.0f, 20.0f, extent.x + 36.0f,
                         44.0f};
    ui::drawPanel(rect, msg.isError ? palette::danger : palette::success, 0.4f);
    ui::drawTextInRect(msg.text, rect, 22.0f, palette::textStrong);
}

void MapEditorScreen::draw() {
    const Regions regions = computeRegions();
    drawBrickGrid(regions.brickArea);
    drawLauncherGrid(regions.launcherArea);
    if (!regions.isPortrait) {
        drawToolPanel(regions.tools);
        drawPropertyPanel(regions.properties);
    } else {
        drawPanelTabs(regions.panelTabs);
        if (portraitPanel == PortraitPanel::Tools) {
            drawToolPanel(regions.tools);
        } else {
            drawPropertyPanel(regions.properties);
        }
    }
    drawMessage();
}
