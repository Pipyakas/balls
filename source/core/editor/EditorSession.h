#pragma once

#include "editor/LevelEditor.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class EditorTool { Paint, Extend, Erase, Pick };

enum class EditorShortcut {
    Undo,
    Redo,
    Save,
    Paint,
    Extend,
    Erase,
    Pick,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
};

enum class LauncherClick {
    Set,
    Clear,
    DeleteRow,
};

enum class EditorLeave { Confirm, Leave };

struct EditorToolButton {
    EditorTool tool;
    std::string_view label;
};

struct EditorMessage {
    std::string text;
    bool isError = false;
    float secondsLeft = 0.0f;
};

inline constexpr int editorMaximumHits = 999;
inline constexpr int editorMaximumLauncherBalls = 100000;
inline constexpr int editorMaximumBoardSize = 200;
inline constexpr int editorMaximumTotalRows = 400;
inline constexpr int editorMaximumLauncherColumns = 8;
inline constexpr int editorMaximumVisibleRows = 8;
inline constexpr size_t editorMaximumNameLength = 40;

inline constexpr int editorShiftStep = 10;

class EditorSession {
public:

    EditorSession(GameMode mode, std::optional<LevelFiles> files, std::filesystem::path normalLevels,
                  std::filesystem::path performanceLevels);

    void update(float deltaSeconds);

    const LevelData& level() const { return editor.level(); }
    const EditorValidation& validation() const { return currentValidation; }

    EditorTool tool() const { return currentTool; }
    void setTool(EditorTool tool);
    BallColor brushColor() const { return currentColor; }

    void selectColor(BallColor color);
    int brushHits() const { return currentHits; }

    void setBrushHits(int hits);
    int launcherBalls() const { return launcherBallCount; }
    void setLauncherBalls(int balls);
    int selectedBrick() const { return selectedBrickId; }

    std::string selectionText() const;

    void beginStroke(bool erase);
    void strokeCell(int column, int row);
    void endStroke();
    void clickLauncher(int column, int row, LauncherClick click);

    const std::string& name() const { return levelName; }

    void setName(std::string_view name);
    GameMode mode() const { return targetMode; }
    void setMode(GameMode mode);
    void setBoardSize(int width, int brickRows, int totalRows);
    void setLauncherColumns(int columns);
    void setVisibleRows(int rows);
    void autoFillLaunchers();

    bool canUndo() const { return editor.canUndo(); }
    bool canRedo() const { return editor.canRedo(); }
    void undo();
    void redo();
    bool isDirty() const { return editor.isDirty(); }

    bool isConfirmingLeave() const { return confirmLeave; }

    bool save();

    std::optional<LevelFiles> saveAndPlay();
    EditorLeave back();

    bool handleShortcut(EditorShortcut shortcut);

    std::string saveLabel() const;
    std::string_view backLabel() const;

    std::string footerText() const;
    static std::span<const EditorToolButton> toolButtons();

    std::string colorTallyText(BallColor color) const;
    bool colorTallyMatches(BallColor color) const;

    const EditorMessage& message() const { return currentMessage; }

private:
    const std::filesystem::path& folderFor(GameMode mode) const;
    void showMessage(std::string text, bool isError);
    void newLevel();

    void edited();
    void applyBrickTool(int column, int row);

    LevelEditor editor;
    std::array<std::filesystem::path, 2> folders;
    GameMode targetMode;
    std::string levelName;
    EditorTool currentTool = EditorTool::Paint;
    BallColor currentColor = BallColor::Red;
    int currentHits = 1;
    int launcherBallCount = 10;
    int selectedBrickId = emptyBrickId;
    bool strokeActive = false;
    bool strokeErases = false;
    bool strokeDragging = false;
    std::optional<std::pair<int, int>> lastStrokeCell;
    bool confirmLeave = false;
    EditorValidation currentValidation;
    EditorMessage currentMessage;
};
