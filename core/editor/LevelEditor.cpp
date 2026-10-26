#include "editor/LevelEditor.h"

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int minBoardExtent = 1;
constexpr int minLauncherExtent = 1;
constexpr int minBrickHits = 1;
constexpr int minNormalColors = 3;
constexpr int minPerformanceColors = 7;
constexpr int maxUndoSteps = 100;

struct LauncherChunk {
    BallColor color = BallColor::Red;
    int count = 0;
};

bool touchesBrickId(const std::vector<int>& cellIds, int width, int height, int column, int row, int brickId) {
    const int candidates[4][2] = {{column - 1, row}, {column + 1, row}, {column, row - 1}, {column, row + 1}};
    for (const auto& candidate : candidates) {
        if (candidate[0] < 0 || candidate[0] >= width || candidate[1] < 0 || candidate[1] >= height) {
            continue;
        }
        const size_t index =
            static_cast<size_t>(candidate[1]) * static_cast<size_t>(width) + static_cast<size_t>(candidate[0]);
        if (cellIds[index] == brickId) {
            return true;
        }
    }
    return false;
}

bool sameBrickTypes(std::vector<BrickType> left, std::vector<BrickType> right) {
    if (left.size() != right.size()) {
        return false;
    }
    const auto byId = [](const BrickType& first, const BrickType& second) { return first.id < second.id; };
    std::ranges::sort(left, byId);
    std::ranges::sort(right, byId);
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].id != right[i].id || left[i].color != right[i].color || left[i].hits != right[i].hits) {
            return false;
        }
    }
    return true;
}

bool sameLaunchers(const LauncherLayout& left, const LauncherLayout& right) {
    if (left.columns != right.columns || left.visibleRows != right.visibleRows ||
        left.slots.size() != right.slots.size()) {
        return false;
    }
    for (size_t i = 0; i < left.slots.size(); ++i) {
        const std::optional<Launcher>& first = left.slots[i];
        const std::optional<Launcher>& second = right.slots[i];
        if (first.has_value() != second.has_value()) {
            return false;
        }
        if (first && (first->color != second->color || first->ballCount != second->ballCount)) {
            return false;
        }
    }
    return true;
}

bool sameLevels(const LevelData& left, const LevelData& right) {
    return left.name == right.name && left.bricks.width == right.bricks.width &&
           left.bricks.height == right.bricks.height && left.bricks.playableHeight == right.bricks.playableHeight &&
           left.bricks.cellIds == right.bricks.cellIds && sameBrickTypes(left.bricks.types, right.bricks.types) &&
           sameLaunchers(left.launchers, right.launchers);
}

}

void LevelEditor::newLevel(std::string name, int width, int height, int playableHeight, int launcherColumns,
                           int visibleRows) {
    width = std::max(width, minBoardExtent);
    height = std::max(height, minBoardExtent);
    LevelData fresh;
    fresh.name = std::move(name);
    fresh.bricks.width = width;
    fresh.bricks.height = height;
    fresh.bricks.playableHeight = std::max(playableHeight, height);
    fresh.bricks.cellIds.assign(static_cast<size_t>(width) * static_cast<size_t>(height), emptyBrickId);
    fresh.launchers.columns = std::max(launcherColumns, minLauncherExtent);
    fresh.launchers.visibleRows = std::max(visibleRows, minLauncherExtent);
    levelData = fresh;
    savedLevel = levelData;
    undoStack.clear();
    redoStack.clear();
}

void LevelEditor::load(LevelData level) {
    levelData = std::move(level);
    savedLevel = levelData;
    undoStack.clear();
    redoStack.clear();
}

void LevelEditor::resizeBoard(int width, int height, int playableHeight) {
    width = std::max(width, minBoardExtent);
    height = std::max(height, minBoardExtent);
    const BrickLayout& current = levelData.bricks;
    BrickLayout resized;
    resized.width = width;
    resized.height = height;
    resized.playableHeight = std::max(playableHeight, height);
    resized.cellIds.assign(static_cast<size_t>(width) * static_cast<size_t>(height), emptyBrickId);
    resized.types = current.types;
    const int overlapWidth = std::min(width, current.width);
    const int overlapHeight = std::min(height, current.height);
    for (int row = 0; row < overlapHeight; ++row) {
        for (int column = 0; column < overlapWidth; ++column) {
            resized.cellIds[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)] =
                current.cellId(column, row);
        }
    }
    levelData.bricks = std::move(resized);
    pruneUnusedTypes();
}

int LevelEditor::brickIdAt(int column, int row) const {
    if (!insideBricks(column, row)) {
        return emptyBrickId;
    }
    return levelData.bricks.cellId(column, row);
}

void LevelEditor::paintCell(int column, int row, BallColor color, int hits) {
    if (!insideBricks(column, row)) {
        return;
    }
    BrickLayout& bricks = levelData.bricks;
    const int freshId = freshBrickId();
    bricks.cellIds[static_cast<size_t>(row) * static_cast<size_t>(bricks.width) + static_cast<size_t>(column)] =
        freshId;
    bricks.types.push_back(BrickType{freshId, color, std::max(hits, minBrickHits)});
    pruneUnusedTypes();
}

bool LevelEditor::extendBrick(int brickId, int column, int row) {
    BrickLayout& bricks = levelData.bricks;
    if (bricks.findType(brickId) == nullptr || !insideBricks(column, row)) {
        return false;
    }
    const size_t index =
        static_cast<size_t>(row) * static_cast<size_t>(bricks.width) + static_cast<size_t>(column);
    if (bricks.cellIds[index] == brickId) {
        return true;
    }
    if (!touchesBrickId(bricks.cellIds, bricks.width, bricks.height, column, row, brickId)) {
        return false;
    }
    bricks.cellIds[index] = brickId;
    pruneUnusedTypes();
    return true;
}

void LevelEditor::eraseCell(int column, int row) {
    if (!insideBricks(column, row)) {
        return;
    }
    BrickLayout& bricks = levelData.bricks;
    const size_t erasedIndex =
        static_cast<size_t>(row) * static_cast<size_t>(bricks.width) + static_cast<size_t>(column);
    const int erasedId = bricks.cellIds[erasedIndex];
    if (erasedId == emptyBrickId || bricks.findType(erasedId) == nullptr) {
        return;
    }
    const BallColor keptColor = bricks.findType(erasedId)->color;
    const int keptHits = bricks.findType(erasedId)->hits;
    bricks.cellIds[erasedIndex] = emptyBrickId;
    const int width = bricks.width;
    const int height = bricks.height;
    std::vector<char> visited(bricks.cellIds.size(), 0);
    std::vector<std::vector<size_t>> pieces;
    for (size_t start = 0; start < bricks.cellIds.size(); ++start) {
        if (bricks.cellIds[start] != erasedId || visited[start]) {
            continue;
        }
        std::vector<size_t> piece{start};
        visited[start] = 1;
        for (size_t cursor = 0; cursor < piece.size(); ++cursor) {
            const int pieceColumn = static_cast<int>(piece[cursor] % static_cast<size_t>(width));
            const int pieceRow = static_cast<int>(piece[cursor] / static_cast<size_t>(width));
            const int candidates[4][2] = {
                {pieceColumn - 1, pieceRow}, {pieceColumn + 1, pieceRow}, {pieceColumn, pieceRow - 1}, {pieceColumn, pieceRow + 1}};
            for (const auto& candidate : candidates) {
                if (candidate[0] < 0 || candidate[0] >= width || candidate[1] < 0 || candidate[1] >= height) {
                    continue;
                }
                const size_t candidateIndex = static_cast<size_t>(candidate[1]) * static_cast<size_t>(width) +
                                              static_cast<size_t>(candidate[0]);
                if (bricks.cellIds[candidateIndex] != erasedId || visited[candidateIndex]) {
                    continue;
                }
                visited[candidateIndex] = 1;
                piece.push_back(candidateIndex);
            }
        }
        pieces.push_back(std::move(piece));
    }
    if (pieces.size() > 1) {
        for (const auto& piece : pieces) {
            const int pieceId = freshBrickId();
            bricks.types.push_back(BrickType{pieceId, keptColor, keptHits});
            for (const size_t index : piece) {
                bricks.cellIds[index] = pieceId;
            }
        }
    }
    pruneUnusedTypes();
}

void LevelEditor::setBrickHits(int brickId, int hits) {
    for (BrickType& type : levelData.bricks.types) {
        if (type.id == brickId) {
            type.hits = std::max(hits, minBrickHits);
        }
    }
}

void LevelEditor::setBrickColor(int brickId, BallColor color) {
    for (BrickType& type : levelData.bricks.types) {
        if (type.id == brickId) {
            type.color = color;
        }
    }
}

void LevelEditor::setLauncherColumns(int columns) {
    columns = std::max(columns, minLauncherExtent);
    LauncherLayout& launchers = levelData.launchers;
    const int rows = launchers.rowCount();
    const int oldColumns = launchers.columns;
    std::vector<std::optional<Launcher>> reshaped(static_cast<size_t>(rows) * static_cast<size_t>(columns));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns && column < oldColumns; ++column) {
            reshaped[static_cast<size_t>(row) * static_cast<size_t>(columns) + static_cast<size_t>(column)] =
                launchers.slot(column, row);
        }
    }
    launchers.columns = columns;
    launchers.slots = std::move(reshaped);
}

void LevelEditor::setVisibleRows(int rows) {
    levelData.launchers.visibleRows = std::max(rows, minLauncherExtent);
}

void LevelEditor::setLauncher(int column, int row, std::optional<Launcher> launcher) {
    LauncherLayout& launchers = levelData.launchers;
    if (column < 0 || column >= launchers.columns || row < 0) {
        return;
    }
    while (launchers.rowCount() <= row) {
        launchers.slots.insert(launchers.slots.end(), static_cast<size_t>(launchers.columns), std::nullopt);
    }
    launchers.slots[static_cast<size_t>(row) * static_cast<size_t>(launchers.columns) + static_cast<size_t>(column)] =
        launcher;
}

void LevelEditor::removeLauncherRow(int row) {
    LauncherLayout& launchers = levelData.launchers;
    if (row < 0 || row >= launchers.rowCount()) {
        return;
    }
    const auto first = launchers.slots.begin() + static_cast<size_t>(row) * static_cast<size_t>(launchers.columns);
    launchers.slots.erase(first, first + launchers.columns);
}

void LevelEditor::autoFillLaunchers(int ballsPerLauncher) {
    ballsPerLauncher = std::max(ballsPerLauncher, minLauncherExtent);
    LauncherLayout& launchers = levelData.launchers;
    if (launchers.columns < minLauncherExtent) {
        launchers.columns = minLauncherExtent;
    }
    const ColorTally hits = countBrickHitsByColor(levelData.bricks);
    std::vector<LauncherChunk> chunks;
    for (const BallColor color : allBallColors) {
        int remaining = hits[toIndex(color)];
        while (remaining > 0) {
            const int take = std::min(remaining, ballsPerLauncher);
            chunks.push_back(LauncherChunk{color, take});
            remaining -= take;
        }
    }
    const size_t columns = static_cast<size_t>(launchers.columns);
    const int rows = chunks.empty() ? 0 : static_cast<int>((chunks.size() + columns - 1) / columns);
    launchers.slots.assign(static_cast<size_t>(rows) * columns, std::nullopt);
    for (size_t i = 0; i < chunks.size(); ++i) {
        launchers.slots[i] = Launcher{chunks[i].color, chunks[i].count};
    }
}

EditorValidation LevelEditor::validate(GameMode mode) const {
    EditorValidation validation;
    validation.brickHits = countBrickHitsByColor(levelData.bricks);
    validation.balls = countBallsByColor(levelData.launchers);
    bool hasBricks = false;
    for (const int id : levelData.bricks.cellIds) {
        if (id != emptyBrickId) {
            hasBricks = true;
            break;
        }
    }
    if (!hasBricks) {
        validation.problems.push_back("level has no bricks");
    }
    for (const BallColor color : allBallColors) {
        const int hits = validation.brickHits[toIndex(color)];
        const int balls = validation.balls[toIndex(color)];
        if (hits > 0 && balls < hits) {
            validation.problems.push_back(
                std::format("{}: {} balls for {} hits", ballColorName(color), balls, hits));
        }
    }
    const int requiredColors = mode == GameMode::Performance ? minPerformanceColors : minNormalColors;
    const int distinctColors = countDistinctColors(validation.brickHits);
    if (distinctColors < requiredColors) {
        validation.problems.push_back(std::format("only {} brick {}, need at least {}", distinctColors,
                                                  distinctColors == 1 ? "color" : "colors", requiredColors));
    }
    bool hasLaunchers = false;
    for (const auto& slot : levelData.launchers.slots) {
        if (slot) {
            hasLaunchers = true;
            break;
        }
    }
    if (!hasLaunchers) {
        validation.problems.push_back("level has no launchers");
    }
    return validation;
}

void LevelEditor::beginEdit() {
    undoStack.push_back(levelData);
    if (undoStack.size() > static_cast<size_t>(maxUndoSteps)) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

bool LevelEditor::undo() {
    if (undoStack.empty()) {
        return false;
    }
    redoStack.push_back(levelData);
    levelData = undoStack.back();
    undoStack.pop_back();
    return true;
}

bool LevelEditor::redo() {
    if (redoStack.empty()) {
        return false;
    }
    undoStack.push_back(levelData);
    levelData = redoStack.back();
    redoStack.pop_back();
    return true;
}

bool LevelEditor::isDirty() const {
    return !sameLevels(levelData, savedLevel);
}

void LevelEditor::markSaved() {
    savedLevel = levelData;
}

int LevelEditor::freshBrickId() const {
    int topId = emptyBrickId;
    for (const int id : levelData.bricks.cellIds) {
        topId = std::max(topId, id);
    }
    for (const BrickType& type : levelData.bricks.types) {
        topId = std::max(topId, type.id);
    }
    return topId + 1;
}

bool LevelEditor::insideBricks(int column, int row) const {
    return column >= 0 && row >= 0 && column < levelData.bricks.width && row < levelData.bricks.height;
}

void LevelEditor::pruneUnusedTypes() {
    BrickLayout& bricks = levelData.bricks;
    const std::set<int> placed(bricks.cellIds.begin(), bricks.cellIds.end());
    std::vector<BrickType> kept;
    kept.reserve(bricks.types.size());
    for (const BrickType& type : bricks.types) {
        if (placed.contains(type.id)) {
            kept.push_back(type);
        }
    }
    bricks.types = std::move(kept);
}
