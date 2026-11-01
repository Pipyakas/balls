#include "editor/EditorSession.h"

#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace {

constexpr const char* newLevelName = "NewLevel";
constexpr int newLevelWidth = 10;
constexpr int newLevelBrickRows = 8;
constexpr int newLevelTotalRows = 20;
constexpr int newLevelLauncherColumns = 3;
constexpr int newLevelVisibleRows = 3;

constexpr std::array<EditorToolButton, 4> toolButtonList{{
    {EditorTool::Paint, "Paint (P)"},
    {EditorTool::Extend, "Extend (E)"},
    {EditorTool::Erase, "Erase (X)"},
    {EditorTool::Pick, "Pick (K)"},
}};

bool allowedInName(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
}

}

EditorSession::EditorSession(GameMode mode, std::optional<LevelFiles> files, std::filesystem::path normalLevels,
                             std::filesystem::path performanceLevels)
    : folders{std::move(normalLevels), std::move(performanceLevels)}, targetMode(mode) {
    if (files) {
        if (auto loaded = loadLevel(*files)) {
            editor.load(std::move(*loaded));
            editor.markSaved();
            levelName = files->name;
            currentValidation = editor.validate(targetMode);
            return;
        } else {
            showMessage("Could not load: " + loaded.error(), true);
        }
    }
    newLevel();
}

void EditorSession::newLevel() {
    levelName = newLevelName;
    editor.newLevel(levelName, newLevelWidth, newLevelBrickRows, newLevelTotalRows, newLevelLauncherColumns,
                    newLevelVisibleRows);
    selectedBrickId = emptyBrickId;
    currentValidation = editor.validate(targetMode);
}

const std::filesystem::path& EditorSession::folderFor(GameMode mode) const {
    return folders[mode == GameMode::Normal ? 0 : 1];
}

void EditorSession::update(float deltaSeconds) {
    currentMessage.secondsLeft = std::max(0.0f, currentMessage.secondsLeft - deltaSeconds);
}

void EditorSession::showMessage(std::string text, bool isError) {
    currentMessage = {std::move(text), isError, theme::toastSeconds};
}

void EditorSession::edited() {
    confirmLeave = false;
    currentValidation = editor.validate(targetMode);
}

void EditorSession::setTool(EditorTool tool) {
    currentTool = tool;
    confirmLeave = false;
}

void EditorSession::selectColor(BallColor color) {
    currentColor = color;
    confirmLeave = false;
    if (currentTool == EditorTool::Pick && selectedBrickId != emptyBrickId) {
        editor.beginEdit();
        editor.setBrickColor(selectedBrickId, color);
        edited();
    }
}

void EditorSession::setBrushHits(int hits) {
    currentHits = std::clamp(hits, 1, editorMaximumHits);
    confirmLeave = false;
    if (currentTool == EditorTool::Pick && selectedBrickId != emptyBrickId) {
        editor.beginEdit();
        editor.setBrickHits(selectedBrickId, currentHits);
        edited();
    }
}

void EditorSession::setLauncherBalls(int balls) {
    launcherBallCount = std::clamp(balls, 1, editorMaximumLauncherBalls);
    confirmLeave = false;
}

std::string EditorSession::selectionText() const {
    return selectedBrickId == emptyBrickId ? std::string("No brick selected")
                                           : std::format("Selected id {}", selectedBrickId);
}

void EditorSession::beginStroke(bool erase) {
    editor.beginEdit();
    strokeActive = true;
    strokeErases = erase;
    strokeDragging = false;
    lastStrokeCell.reset();
    confirmLeave = false;
}

void EditorSession::strokeCell(int column, int row) {
    const BrickLayout& bricks = editor.level().bricks;
    if (!strokeActive || column < 0 || row < 0 || column >= bricks.width || row >= bricks.height) {
        return;
    }
    if (lastStrokeCell && lastStrokeCell->first == column && lastStrokeCell->second == row) {
        return;
    }
    applyBrickTool(column, row);

    strokeDragging = lastStrokeCell.has_value() || currentTool != EditorTool::Extend || selectedBrickId != emptyBrickId;
    lastStrokeCell = std::make_pair(column, row);
    edited();
}

void EditorSession::applyBrickTool(int column, int row) {
    if (strokeErases || currentTool == EditorTool::Erase) {
        editor.eraseCell(column, row);
        return;
    }
    const int brickUnderCursor = editor.brickIdAt(column, row);
    switch (currentTool) {
    case EditorTool::Paint:
        editor.paintCell(column, row, currentColor, currentHits);
        break;
    case EditorTool::Extend:
        if (!strokeDragging && brickUnderCursor != emptyBrickId) {
            selectedBrickId = brickUnderCursor;
        } else if (selectedBrickId != emptyBrickId) {
            editor.extendBrick(selectedBrickId, column, row);
        }
        break;
    case EditorTool::Pick:
        selectedBrickId = brickUnderCursor;
        if (const BrickType* type = editor.level().bricks.findType(brickUnderCursor)) {
            currentColor = type->color;
            currentHits = type->hits;
        }
        break;
    case EditorTool::Erase:
        break;
    }
}

void EditorSession::endStroke() {
    strokeActive = false;
    strokeDragging = false;
    lastStrokeCell.reset();
}

void EditorSession::clickLauncher(int column, int row, LauncherClick click) {
    const LauncherLayout& launchers = editor.level().launchers;
    if (column < 0 || row < 0 || column >= launchers.columns) {
        return;
    }

    if (click != LauncherClick::Set && row >= launchers.rowCount()) {
        return;
    }
    editor.beginEdit();
    switch (click) {
    case LauncherClick::Set:
        editor.setLauncher(column, row, Launcher{currentColor, launcherBallCount});
        break;
    case LauncherClick::Clear:
        editor.setLauncher(column, row, std::nullopt);
        break;
    case LauncherClick::DeleteRow:
        editor.removeLauncherRow(row);
        break;
    }
    edited();
}

void EditorSession::setName(std::string_view name) {
    std::string kept;
    for (const char character : name) {
        if (allowedInName(character) && kept.size() < editorMaximumNameLength) {
            kept.push_back(character);
        }
    }
    if (kept != levelName) {
        levelName = std::move(kept);
        confirmLeave = false;
    }
}

void EditorSession::setMode(GameMode mode) {
    targetMode = mode;
    edited();
}

void EditorSession::setBoardSize(int width, int brickRows, int totalRows) {
    const int clampedWidth = std::clamp(width, 1, editorMaximumBoardSize);
    const int clampedRows = std::clamp(brickRows, 1, editorMaximumBoardSize);
    const int clampedTotal = std::clamp(totalRows, clampedRows, editorMaximumTotalRows);
    const BrickLayout& bricks = editor.level().bricks;
    if (clampedWidth == bricks.width && clampedRows == bricks.height && clampedTotal == bricks.playableHeight) {
        return;
    }
    editor.beginEdit();
    editor.resizeBoard(clampedWidth, clampedRows, clampedTotal);
    edited();
}

void EditorSession::setLauncherColumns(int columns) {
    const int clamped = std::clamp(columns, 1, editorMaximumLauncherColumns);
    if (clamped == editor.level().launchers.columns) {
        return;
    }
    editor.beginEdit();
    editor.setLauncherColumns(clamped);
    edited();
}

void EditorSession::setVisibleRows(int rows) {
    const int clamped = std::clamp(rows, 1, editorMaximumVisibleRows);
    if (clamped == editor.level().launchers.visibleRows) {
        return;
    }
    editor.beginEdit();
    editor.setVisibleRows(clamped);
    edited();
}

void EditorSession::autoFillLaunchers() {
    editor.beginEdit();
    editor.autoFillLaunchers(launcherBallCount);
    edited();
}

void EditorSession::undo() {
    if (editor.undo()) {
        selectedBrickId = editor.level().bricks.findType(selectedBrickId) ? selectedBrickId : emptyBrickId;
        edited();
    }
}

void EditorSession::redo() {
    if (editor.redo()) {
        selectedBrickId = editor.level().bricks.findType(selectedBrickId) ? selectedBrickId : emptyBrickId;
        edited();
    }
}

bool EditorSession::save() {
    confirmLeave = false;
    if (levelName.empty()) {
        showMessage("Enter a level name first", true);
        return false;
    }
    LevelData level = editor.level();
    level.name = levelName;
    if (auto result = saveLevel(level, levelFilesFor(folderFor(targetMode), levelName)); !result) {
        showMessage(result.error(), true);
        return false;
    }
    editor.markSaved();
    showMessage(std::format("Saved {} ({})", levelName, gameModeName(targetMode)), false);
    return true;
}

std::optional<LevelFiles> EditorSession::saveAndPlay() {
    if (!currentValidation.isPlayable() || !save()) {
        return std::nullopt;
    }
    return levelFilesFor(folderFor(targetMode), levelName);
}

EditorLeave EditorSession::back() {
    if (editor.isDirty() && !confirmLeave) {
        confirmLeave = true;
        return EditorLeave::Confirm;
    }
    confirmLeave = false;
    return EditorLeave::Leave;
}

bool EditorSession::handleShortcut(EditorShortcut shortcut) {
    switch (shortcut) {
    case EditorShortcut::Undo:
        if (!canUndo()) {
            return false;
        }
        undo();
        return true;
    case EditorShortcut::Redo:
        if (!canRedo()) {
            return false;
        }
        redo();
        return true;
    case EditorShortcut::Save:
        return save();
    case EditorShortcut::Paint:
        setTool(EditorTool::Paint);
        return true;
    case EditorShortcut::Extend:
        setTool(EditorTool::Extend);
        return true;
    case EditorShortcut::Erase:
        setTool(EditorTool::Erase);
        return true;
    case EditorShortcut::Pick:
        setTool(EditorTool::Pick);
        return true;
    default:
        selectColor(ballColorFromIndex(static_cast<int>(shortcut) - static_cast<int>(EditorShortcut::Color1)));
        return true;
    }
}

std::string EditorSession::saveLabel() const { return editor.isDirty() ? "Save *  (Ctrl+S)" : "Save  (Ctrl+S)"; }

std::string_view EditorSession::backLabel() const { return confirmLeave ? "Discard changes?" : "Back"; }

std::string EditorSession::footerText() const {
    const BrickLayout& bricks = editor.level().bricks;
    return std::format("{} x {}  |  L-drag: tool  R-drag: erase  Ctrl+Z/Y", bricks.width, bricks.height);
}

std::span<const EditorToolButton> EditorSession::toolButtons() { return toolButtonList; }

std::string EditorSession::colorTallyText(BallColor color) const {
    const size_t index = static_cast<size_t>(toIndex(color));
    return std::format("{}/{}", currentValidation.balls[index], currentValidation.brickHits[index]);
}

bool EditorSession::colorTallyMatches(BallColor color) const {
    const size_t index = static_cast<size_t>(toIndex(color));
    return currentValidation.balls[index] == currentValidation.brickHits[index];
}
