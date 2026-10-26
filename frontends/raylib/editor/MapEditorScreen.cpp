#include "editor/MapEditorScreen.h"

#include "app/App.h"
#include "app/Paths.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <format>

namespace {

constexpr float margin = 16.0f;
constexpr float sidePanelWidth = 270.0f;
constexpr float propertyPanelWidth = 330.0f;
constexpr float brickAreaShare = 0.66f;
constexpr float controlHeight = 38.0f;
constexpr float controlGap = 8.0f;
constexpr float messageSeconds = 3.0f;
constexpr int maximumBoardSize = 200;
constexpr int maximumHits = 999;
constexpr int maximumLauncherBalls = 100000;
constexpr size_t maximumNameLength = 40;
constexpr const char* defaultLevelName = "NewLevel";
constexpr int defaultWidth = 10;
constexpr int defaultHeight = 8;
constexpr int defaultPlayableHeight = 20;
constexpr int defaultLauncherColumns = 3;
constexpr int defaultVisibleRows = 3;
constexpr int autoFillBallsPerLauncher = 10;

constexpr std::array<std::pair<EditorTool, const char*>, 4> toolNames{{
    {EditorTool::Paint, "Paint (P)"},
    {EditorTool::Extend, "Extend (E)"},
    {EditorTool::Erase, "Erase (X)"},
    {EditorTool::Pick, "Pick (K)"},
}};

Rectangle nextControl(Rectangle area, float& cursor, float height = controlHeight) {
    const Rectangle rect{area.x + 12.0f, cursor, area.width - 24.0f, height};
    cursor += height + controlGap;
    return rect;
}

void sectionTitle(Rectangle area, float& cursor, std::string_view text) {
    cursor += 6.0f;
    ui::drawText(text, {area.x + 12.0f, cursor}, 22.0f, palette::accent);
    cursor += 30.0f;
}

}

MapEditorScreen::MapEditorScreen(App& app, GameMode mode, std::optional<LevelFiles> files)
    : app(app), targetMode(mode) {
    if (files) {
        if (auto level = loadLevel(*files)) {
            editor.load(std::move(*level));
            levelName = files->name;
            editor.markSaved();
            return;
        } else {
            showMessage("Could not load: " + level.error(), true);
        }
    }
    levelName = defaultLevelName;
    editor.newLevel(levelName, defaultWidth, defaultHeight, defaultPlayableHeight, defaultLauncherColumns,
                    defaultVisibleRows);
}

void MapEditorScreen::showMessage(std::string text, bool isError) {
    message = std::move(text);
    messageSeconds = ::messageSeconds;
    messageIsError = isError;
}

MapEditorScreen::Regions MapEditorScreen::computeRegions() const {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    Regions regions;
    regions.tools = {margin, margin, sidePanelWidth, height - margin * 2.0f};
    regions.properties = {width - propertyPanelWidth - margin, margin, propertyPanelWidth, height - margin * 2.0f};
    const float centerX = regions.tools.x + regions.tools.width + margin;
    const float centerWidth = regions.properties.x - margin - centerX;
    const float brickHeight = (height - margin * 3.0f) * brickAreaShare;
    regions.brickArea = {centerX, margin, centerWidth, brickHeight};
    regions.launcherArea = {centerX, margin * 2.0f + brickHeight, centerWidth, height - margin * 3.0f - brickHeight};
    return regions;
}

float MapEditorScreen::brickCellSize(Rectangle area) const {
    const BrickLayout& bricks = editor.level().bricks;
    const Rectangle inner = ui::shrink(area, 14.0f);
    return std::max(2.0f, std::min(inner.width / static_cast<float>(std::max(bricks.width, 1)),
                                   inner.height / static_cast<float>(std::max(bricks.playableHeight, 1))));
}

Rectangle MapEditorScreen::brickGridRect(Rectangle area) const {
    const BrickLayout& bricks = editor.level().bricks;
    const float cell = brickCellSize(area);
    const float gridWidth = cell * static_cast<float>(bricks.width);
    const float gridHeight = cell * static_cast<float>(bricks.playableHeight);
    return {area.x + (area.width - gridWidth) * 0.5f, area.y + (area.height - gridHeight) * 0.5f, gridWidth, gridHeight};
}

float MapEditorScreen::launcherCellSize(Rectangle area) const {
    const LauncherLayout& launchers = editor.level().launchers;
    const Rectangle inner = ui::shrink(area, 14.0f);
    const float rows = static_cast<float>(launchers.rowCount() + 1);
    const float columns = static_cast<float>(std::max(launchers.columns, 1));
    return std::max(14.0f, std::min({inner.width / columns, inner.height / rows, 64.0f}));
}

Rectangle MapEditorScreen::launcherGridRect(Rectangle area) const {
    const LauncherLayout& launchers = editor.level().launchers;
    const float cell = launcherCellSize(area);
    const float gridWidth = cell * static_cast<float>(launchers.columns);
    const float gridHeight = cell * static_cast<float>(launchers.rowCount() + 1);
    return {area.x + (area.width - gridWidth) * 0.5f, area.y + 14.0f, gridWidth, gridHeight};
}

MapEditorScreen::GridCell MapEditorScreen::brickCellAt(Rectangle area, Vector2 point) const {
    const Rectangle grid = brickGridRect(area);
    const BrickLayout& bricks = editor.level().bricks;
    if (!CheckCollisionPointRec(point, grid)) {
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

MapEditorScreen::GridCell MapEditorScreen::launcherCellAt(Rectangle area, Vector2 point) const {
    const Rectangle grid = launcherGridRect(area);
    if (!CheckCollisionPointRec(point, grid)) {
        return {};
    }
    const float cell = launcherCellSize(area);
    return {static_cast<int>((point.x - grid.x) / cell), static_cast<int>((point.y - grid.y) / cell)};
}

void MapEditorScreen::selectColor(BallColor color) {
    brushColor = color;
    if (tool == EditorTool::Pick && selectedBrick >= 0) {
        editor.beginEdit();
        editor.setBrickColor(selectedBrick, color);
    }
}

void MapEditorScreen::changeSelectedHits(int hits) {
    if (tool == EditorTool::Pick && selectedBrick >= 0) {
        editor.beginEdit();
        editor.setBrickHits(selectedBrick, hits);
    }
}

bool MapEditorScreen::save() {
    if (levelName.empty()) {
        showMessage("Enter a level name first", true);
        return false;
    }
    LevelData level = editor.level();
    level.name = levelName;
    const LevelFiles files = levelFilesFor(levelDirectory(targetMode), levelName);
    if (auto result = saveLevel(level, files); !result) {
        showMessage(result.error(), true);
        return false;
    }
    editor.markSaved();
    showMessage(std::format("Saved {} ({})", levelName, targetMode == GameMode::Normal ? "normal" : "performance"),
                false);
    return true;
}

void MapEditorScreen::handleShortcuts() {
    if (nameFocused) {
        return;
    }
    const bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (control && IsKeyPressed(KEY_Z)) {
        editor.undo();
    }
    if (control && IsKeyPressed(KEY_Y)) {
        editor.redo();
    }
    if (control && IsKeyPressed(KEY_S)) {
        save();
    }
    if (control) {
        return;
    }
    for (int i = 0; i < ballColorCount; ++i) {
        if (IsKeyPressed(static_cast<KeyboardKey>(KEY_ONE + i))) {
            selectColor(ballColorFromIndex(i));
        }
    }
    if (IsKeyPressed(KEY_P)) {
        tool = EditorTool::Paint;
    }
    if (IsKeyPressed(KEY_E)) {
        tool = EditorTool::Extend;
    }
    if (IsKeyPressed(KEY_X)) {
        tool = EditorTool::Erase;
    }
    if (IsKeyPressed(KEY_K)) {
        tool = EditorTool::Pick;
    }
}

void MapEditorScreen::applyBrickTool(GridCell cell, bool erasing) {
    if (erasing || tool == EditorTool::Erase) {
        editor.eraseCell(cell.column, cell.row);
        return;
    }
    const int brickUnderCursor = editor.brickIdAt(cell.column, cell.row);
    switch (tool) {
    case EditorTool::Paint:
        editor.paintCell(cell.column, cell.row, brushColor, brushHits);
        break;
    case EditorTool::Extend:
        if (!draggingBricks && brickUnderCursor != emptyBrickId) {
            selectedBrick = brickUnderCursor;
        } else if (selectedBrick >= 0) {
            editor.extendBrick(selectedBrick, cell.column, cell.row);
        }
        break;
    case EditorTool::Pick:
        selectedBrick = brickUnderCursor;
        if (const BrickType* type = editor.level().bricks.findType(brickUnderCursor)) {
            brushColor = type->color;
            brushHits = type->hits;
        }
        break;
    case EditorTool::Erase:
        break;
    }
}

void MapEditorScreen::handleBrickInput(Rectangle area) {
    const bool leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool rightDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (!leftDown && !rightDown) {
        draggingBricks = false;
        strokeActive = false;
        lastPaintedCell = {};
        return;
    }
    const GridCell cell = brickCellAt(area, GetMousePosition());
    const bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (pressed && cell.isValid()) {
        editor.beginEdit();
        strokeActive = true;
        draggingBricks = false;
        lastPaintedCell = {};
    }
    if (!strokeActive || !cell.isValid()) {
        return;
    }
    if (cell.column == lastPaintedCell.column && cell.row == lastPaintedCell.row) {
        return;
    }
    applyBrickTool(cell, rightDown);
    draggingBricks = lastPaintedCell.isValid() || tool != EditorTool::Extend || selectedBrick >= 0;
    lastPaintedCell = cell;
}

void MapEditorScreen::handleLauncherInput(Rectangle area) {
    const bool leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (!leftPressed && !rightPressed) {
        return;
    }
    const GridCell cell = launcherCellAt(area, GetMousePosition());
    if (!cell.isValid()) {
        return;
    }
    editor.beginEdit();
    const bool removeRow = rightPressed && IsKeyDown(KEY_LEFT_SHIFT);
    if (removeRow) {
        if (cell.row < editor.level().launchers.rowCount()) {
            editor.removeLauncherRow(cell.row);
        }
        return;
    }
    if (rightPressed) {
        if (cell.row < editor.level().launchers.rowCount()) {
            editor.setLauncher(cell.column, cell.row, std::nullopt);
        }
        return;
    }
    editor.setLauncher(cell.column, cell.row, Launcher{brushColor, launcherBallCount});
}

void MapEditorScreen::update(float deltaTime) {
    messageSeconds = std::max(0.0f, messageSeconds - deltaTime);
    if (IsKeyPressed(KEY_ESCAPE) && !nameFocused) {
        app.showMainMenu();
        return;
    }
    handleShortcuts();
    const Regions regions = computeRegions();
    handleBrickInput(regions.brickArea);
    handleLauncherInput(regions.launcherArea);
}

void MapEditorScreen::drawToolPanel(Rectangle area) {
    ui::drawPanel(area, palette::panel, 0.03f);
    float cursor = area.y + 8.0f;
    sectionTitle(area, cursor, "Tool");
    for (const auto& [kind, name] : toolNames) {
        if (ui::button(nextControl(area, cursor), name, true, tool == kind)) {
            tool = kind;
        }
    }

    sectionTitle(area, cursor, "Color (1-7)");
    const Rectangle swatchRow = nextControl(area, cursor, 34.0f);
    const Rectangle swatchRow2 = nextControl(area, cursor, 34.0f);
    for (int i = 0; i < ballColorCount; ++i) {
        const Rectangle row = i < 4 ? swatchRow : swatchRow2;
        const Rectangle swatch = ui::columnSlice(row, i % 4, 4, 8.0f);
        const BallColor color = ballColorFromIndex(i);
        DrawRectangleRounded(swatch, 0.3f, 6, palette::ballColor(color));
        if (color == brushColor) {
            DrawRectangleRoundedLinesEx(ui::shrink(swatch, -3.0f), 0.3f, 6, 3.0f, palette::text);
        }
        if (ui::isClicked(swatch)) {
            selectColor(color);
        }
    }

    sectionTitle(area, cursor, "Brick");
    if (ui::stepper(nextControl(area, cursor), "Hits", brushHits, 1, maximumHits)) {
        changeSelectedHits(brushHits);
    }
    const std::string selection =
        selectedBrick >= 0 ? std::format("Selected id {}", selectedBrick) : std::string("No brick selected");
    ui::drawTextInRect(selection, nextControl(area, cursor, 24.0f), 18.0f, palette::textMuted, ui::Align::Left);

    sectionTitle(area, cursor, "Launcher");
    ui::stepper(nextControl(area, cursor), "Balls", launcherBallCount, 1, maximumLauncherBalls);
    ui::drawTextInRect("L-click set, R-click clear", nextControl(area, cursor, 22.0f), 16.0f, palette::textMuted,
                       ui::Align::Left);
    ui::drawTextInRect("Shift+R-click delete row", nextControl(area, cursor, 22.0f), 16.0f, palette::textMuted,
                       ui::Align::Left);
}

void MapEditorScreen::drawPropertyPanel(Rectangle area) {
    ui::drawPanel(area, palette::panel, 0.03f);
    float cursor = area.y + 8.0f;
    sectionTitle(area, cursor, "Level");
    ui::textField(nextControl(area, cursor), levelName, nameFocused, maximumNameLength);
    const Rectangle modeRow = nextControl(area, cursor);
    if (ui::button(ui::columnSlice(modeRow, 0, 2, 8.0f), "Normal", true, targetMode == GameMode::Normal)) {
        targetMode = GameMode::Normal;
    }
    if (ui::button(ui::columnSlice(modeRow, 1, 2, 8.0f), "Perf", true, targetMode == GameMode::Performance)) {
        targetMode = GameMode::Performance;
    }

    sectionTitle(area, cursor, "Board");
    const BrickLayout& bricks = editor.level().bricks;
    int width = bricks.width;
    int height = bricks.height;
    int playable = bricks.playableHeight;
    const bool widthChanged = ui::stepper(nextControl(area, cursor), "Width", width, 1, maximumBoardSize);
    const bool heightChanged = ui::stepper(nextControl(area, cursor), "Brick rows", height, 1, maximumBoardSize);
    const bool playableChanged =
        ui::stepper(nextControl(area, cursor), "Total rows", playable, height, maximumBoardSize + maximumBoardSize);
    if (widthChanged || heightChanged || playableChanged) {
        editor.beginEdit();
        editor.resizeBoard(width, height, std::max(playable, height));
    }

    sectionTitle(area, cursor, "Launchers");
    int columns = editor.level().launchers.columns;
    int visibleRows = editor.level().launchers.visibleRows;
    if (ui::stepper(nextControl(area, cursor), "Columns", columns, 1, 8)) {
        editor.beginEdit();
        editor.setLauncherColumns(columns);
    }
    if (ui::stepper(nextControl(area, cursor), "Visible rows", visibleRows, 1, 8)) {
        editor.beginEdit();
        editor.setVisibleRows(visibleRows);
    }
    if (ui::button(nextControl(area, cursor), "Auto-fill to match bricks")) {
        editor.beginEdit();
        editor.autoFillLaunchers(launcherBallCount > 0 ? launcherBallCount : autoFillBallsPerLauncher);
    }

    sectionTitle(area, cursor, "Validation");
    const EditorValidation validation = editor.validate(targetMode);
    if (validation.isPlayable()) {
        ui::drawText("Ready to play", {area.x + 12.0f, cursor}, 20.0f, palette::success);
        cursor += 26.0f;
    }
    for (const std::string& problem : validation.problems) {
        if (cursor > area.y + area.height - 200.0f) {
            break;
        }
        ui::drawText(problem, {area.x + 12.0f, cursor}, 17.0f, palette::danger);
        cursor += 22.0f;
    }

    const float buttonsTop = area.y + area.height - (controlHeight + controlGap) * 4.0f - 8.0f;
    cursor = std::max(cursor + 8.0f, buttonsTop);
    const Rectangle undoRow = nextControl(area, cursor);
    if (ui::button(ui::columnSlice(undoRow, 0, 2, 8.0f), "Undo", editor.canUndo())) {
        editor.undo();
    }
    if (ui::button(ui::columnSlice(undoRow, 1, 2, 8.0f), "Redo", editor.canRedo())) {
        editor.redo();
    }
    if (ui::button(nextControl(area, cursor), editor.isDirty() ? "Save *  (Ctrl+S)" : "Save  (Ctrl+S)")) {
        save();
    }
    if (ui::button(nextControl(area, cursor), "Save & Play", validation.isPlayable()) && save()) {
        app.startGame(targetMode, levelFilesFor(levelDirectory(targetMode), levelName));
    }
    if (ui::button(nextControl(area, cursor), confirmLeave ? "Discard changes?" : "Back")) {
        if (editor.isDirty() && !confirmLeave) {
            confirmLeave = true;
        } else {
            app.showMainMenu();
        }
    }
}

void MapEditorScreen::drawBrickGrid(Rectangle area) {
    ui::drawPanel(area, palette::backgroundDeep, 0.02f);
    const BrickLayout& bricks = editor.level().bricks;
    const Rectangle grid = brickGridRect(area);
    const float cell = brickCellSize(area);
    DrawRectangleRec(grid, palette::shade(palette::backgroundDeep, 0.7f));
    DrawRectangleRec({grid.x, grid.y, grid.width, cell * static_cast<float>(bricks.height)}, palette::panel);
    const GridCell hovered = brickCellAt(area, GetMousePosition());
    for (int row = 0; row < bricks.height; ++row) {
        for (int column = 0; column < bricks.width; ++column) {
            const int id = bricks.cellId(column, row);
            const Rectangle rect{grid.x + static_cast<float>(column) * cell, grid.y + static_cast<float>(row) * cell, cell,
                                 cell};
            const BrickType* type = id == emptyBrickId ? nullptr : bricks.findType(id);
            if (type) {
                Color fill = palette::brickColor(type->color);
                if (id == selectedBrick) {
                    fill = palette::shade(fill, 1.35f);
                }
                DrawRectangleRec(rect, fill);
                const float border = std::max(1.0f, cell * 0.08f);
                const Color edge = palette::shade(fill, 0.45f);
                if (column == 0 || bricks.cellId(column - 1, row) != id) {
                    DrawRectangleRec({rect.x, rect.y, border, rect.height}, edge);
                }
                if (column == bricks.width - 1 || bricks.cellId(column + 1, row) != id) {
                    DrawRectangleRec({rect.x + rect.width - border, rect.y, border, rect.height}, edge);
                }
                if (row == 0 || bricks.cellId(column, row - 1) != id) {
                    DrawRectangleRec({rect.x, rect.y, rect.width, border}, edge);
                }
                if (row == bricks.height - 1 || bricks.cellId(column, row + 1) != id) {
                    DrawRectangleRec({rect.x, rect.y + rect.height - border, rect.width, border}, edge);
                }
                const bool firstCellOfBrick = (column == 0 || bricks.cellId(column - 1, row) != id) &&
                                              (row == 0 || bricks.cellId(column, row - 1) != id);
                if (type->hits > 1 && firstCellOfBrick && cell >= 12.0f) {
                    ui::drawTextInRect(std::to_string(type->hits), rect, cell * 0.55f, palette::text);
                }
            } else {
                DrawRectangleLinesEx(rect, 1.0f, Fade(palette::frame, 0.35f));
            }
            if (hovered.column == column && hovered.row == row) {
                DrawRectangleLinesEx(rect, 2.0f, palette::accent);
            }
        }
    }
    ui::drawText(std::format("{} x {}  |  L-drag: tool  R-drag: erase  Ctrl+Z/Y", bricks.width, bricks.height),
                 {area.x + 10.0f, area.y + area.height - 26.0f}, 18.0f, palette::textMuted);
}

void MapEditorScreen::drawLauncherGrid(Rectangle area) {
    ui::drawPanel(area, palette::panel, 0.04f);
    const LauncherLayout& launchers = editor.level().launchers;
    const Rectangle grid = launcherGridRect(area);
    const float cell = launcherCellSize(area);
    const GridCell hovered = launcherCellAt(area, GetMousePosition());
    for (int row = 0; row <= launchers.rowCount(); ++row) {
        for (int column = 0; column < launchers.columns; ++column) {
            const Rectangle rect = ui::shrink(
                {grid.x + static_cast<float>(column) * cell, grid.y + static_cast<float>(row) * cell, cell, cell}, 3.0f);
            const bool exists = row < launchers.rowCount();
            const auto& slot = exists ? launchers.slot(column, row) : std::optional<Launcher>{};
            if (slot) {
                DrawRectangleRounded(rect, 0.25f, 6, palette::ballColor(slot->color));
                ui::drawTextInRect(std::to_string(slot->ballCount), rect, std::max(10.0f, cell * 0.36f), palette::text);
            } else {
                DrawRectangleRoundedLinesEx(rect, 0.25f, 6, 1.5f, Fade(palette::frame, exists ? 0.7f : 0.3f));
            }
            if (row == launchers.visibleRows - 1) {
                DrawLineEx({grid.x, rect.y + rect.height + 4.0f}, {grid.x + grid.width, rect.y + rect.height + 4.0f}, 2.0f,
                           Fade(palette::accent, 0.6f));
            }
            if (hovered.column == column && hovered.row == row) {
                DrawRectangleRoundedLinesEx(rect, 0.25f, 6, 2.5f, palette::accent);
            }
        }
    }
    const EditorValidation validation = editor.validate(targetMode);
    float x = area.x + 12.0f;
    const float y = area.y + area.height - 30.0f;
    for (int color = 0; color < ballColorCount; ++color) {
        if (validation.brickHits[color] == 0 && validation.balls[color] == 0) {
            continue;
        }
        const bool balanced = validation.balls[color] >= validation.brickHits[color];
        DrawCircleV({x + 7.0f, y + 10.0f}, 7.0f, palette::ballColor(color));
        const std::string text = std::format("{}/{}", validation.balls[color], validation.brickHits[color]);
        ui::drawText(text, {x + 18.0f, y}, 18.0f, balanced ? palette::text : palette::danger);
        x += 30.0f + ui::measureText(text, 18.0f).x;
    }
}

void MapEditorScreen::drawMessage() {
    if (messageSeconds <= 0.0f) {
        return;
    }
    const Vector2 extent = ui::measureText(message, 22.0f);
    const Rectangle rect{(static_cast<float>(GetScreenWidth()) - extent.x) * 0.5f - 18.0f, 20.0f, extent.x + 36.0f,
                         44.0f};
    ui::drawPanel(rect, messageIsError ? palette::danger : palette::success, 0.4f);
    ui::drawTextInRect(message, rect, 22.0f, palette::text);
}

void MapEditorScreen::draw() {
    const Regions regions = computeRegions();
    drawBrickGrid(regions.brickArea);
    drawLauncherGrid(regions.launcherArea);
    drawToolPanel(regions.tools);
    drawPropertyPanel(regions.properties);
    drawMessage();
}
