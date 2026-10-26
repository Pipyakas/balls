#pragma once

#include "game/GameMode.h"
#include "level/LevelData.h"

#include <optional>
#include <string>
#include <vector>

struct EditorValidation {
    ColorTally brickHits{};
    ColorTally balls{};
    std::vector<std::string> problems;

    bool isPlayable() const { return problems.empty(); }
};

class LevelEditor {
public:
    void newLevel(std::string name, int width, int height, int playableHeight, int launcherColumns, int visibleRows);
    void load(LevelData level);
    const LevelData& level() const { return levelData; }

    void resizeBoard(int width, int height, int playableHeight);
    int brickIdAt(int column, int row) const;
    void paintCell(int column, int row, BallColor color, int hits);
    bool extendBrick(int brickId, int column, int row);
    void eraseCell(int column, int row);
    void setBrickHits(int brickId, int hits);
    void setBrickColor(int brickId, BallColor color);

    void setLauncherColumns(int columns);
    void setVisibleRows(int rows);
    void setLauncher(int column, int row, std::optional<Launcher> launcher);
    void removeLauncherRow(int row);
    void autoFillLaunchers(int ballsPerLauncher);

    EditorValidation validate(GameMode mode) const;

    void beginEdit();
    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    bool isDirty() const;
    void markSaved();

private:
    int freshBrickId() const;
    bool insideBricks(int column, int row) const;
    void pruneUnusedTypes();

    LevelData levelData;
    LevelData savedLevel;
    std::vector<LevelData> undoStack;
    std::vector<LevelData> redoStack;
};
