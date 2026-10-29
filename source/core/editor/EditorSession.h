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

// The game's map editor ("Map editor") as one state machine for every front end: the tool, brush and launcher
// count, the selected brick, strokes and their undo steps, launcher clicks, the shortcuts, the steppers' ranges, the
// name field's rules, saving with its messages and the unsaved-changes confirmation on Back. LevelEditor keeps the
// level and its undo history; this keeps everything a player does to it. Front ends map pointer and key events onto
// these calls and draw the level, the labels and the message.
enum class EditorTool { Paint, Extend, Erase, Pick };

enum class EditorShortcut {
    Undo,  // Ctrl+Z
    Redo,  // Ctrl+Y
    Save,  // Ctrl+S
    Paint, // P
    Extend, // E
    Erase, // X
    Pick,  // K
    Color1, // 1-7
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
};

enum class LauncherClick {
    Set,       // left click: the brush color and launcher ball count
    Clear,     // right click
    DeleteRow, // Shift + right click
};

// What Back did: asked to confirm unsaved changes (the button now reads "Discard changes?"), or the editor may close.
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

// The game's ranges for the editor's steppers and fields.
inline constexpr int editorMaximumHits = 999;
inline constexpr int editorMaximumLauncherBalls = 100000;
inline constexpr int editorMaximumBoardSize = 200;
inline constexpr int editorMaximumTotalRows = 400;
inline constexpr int editorMaximumLauncherColumns = 8;
inline constexpr int editorMaximumVisibleRows = 8;
inline constexpr size_t editorMaximumNameLength = 40;
// Steppers move by this much with Shift held, else by one.
inline constexpr int editorShiftStep = 10;

class EditorSession {
public:
    // Opens `files` in `mode`, or a new level when there are none or they fail to load (the message says why).
    EditorSession(GameMode mode, std::optional<LevelFiles> files, std::filesystem::path normalLevels,
                  std::filesystem::path performanceLevels);

    void update(float deltaSeconds);

    const LevelData& level() const { return editor.level(); }
    const EditorValidation& validation() const { return currentValidation; }

    EditorTool tool() const { return currentTool; }
    void setTool(EditorTool tool);
    BallColor brushColor() const { return currentColor; }
    // Also recolors the picked brick while the Pick tool is on.
    void selectColor(BallColor color);
    int brushHits() const { return currentHits; }
    // Also changes the picked brick's hits while the Pick tool is on.
    void setBrushHits(int hits);
    int launcherBalls() const { return launcherBallCount; }
    void setLauncherBalls(int balls);
    int selectedBrick() const { return selectedBrickId; }
    // "Selected id N" or "No brick selected".
    std::string selectionText() const;

    // A drag on the brick grid: one undo step from press to release. `erase` is the right button.
    void beginStroke(bool erase);
    void strokeCell(int column, int row);
    void endStroke();
    void clickLauncher(int column, int row, LauncherClick click);

    const std::string& name() const { return levelName; }
    // Keeps only the characters a level file name allows (0-9 A-Z a-z _ -), up to 40.
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
    // Back asked to confirm unsaved changes; any edit or a second Back ends it.
    bool isConfirmingLeave() const { return confirmLeave; }

    bool save();
    // Saves and returns the files to play, when the level is valid and the save worked.
    std::optional<LevelFiles> saveAndPlay();
    EditorLeave back();
    // False when the shortcut does nothing now (the name field has focus, or the action is not available).
    bool handleShortcut(EditorShortcut shortcut);

    std::string saveLabel() const;
    std::string_view backLabel() const;
    // "<W> x <H>  |  L-drag: tool  R-drag: erase  Ctrl+Z/Y".
    std::string footerText() const;
    static std::span<const EditorToolButton> toolButtons();
    // "<balls>/<hits>" under each color, and whether the two match.
    std::string colorTallyText(BallColor color) const;
    bool colorTallyMatches(BallColor color) const;

    const EditorMessage& message() const { return currentMessage; }

private:
    const std::filesystem::path& folderFor(GameMode mode) const;
    void showMessage(std::string text, bool isError);
    void newLevel();
    // Every edit goes through here: it ends a pending Back confirmation and re-validates.
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
