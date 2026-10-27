#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include <optional>
#include <string>

class FBallsApp;

enum class FEditorTool { Paint, Extend, Erase, Pick };

// Front-end state for the shared LevelEditor. Mirrors frontends/raylib/editor/MapEditorScreen:
// tools, brush, level name, messages and the save/play/back flow. The Slate screen only draws it.
class FBallsMapEditor {
public:
    FBallsMapEditor(FBallsApp& App, GameMode Mode, std::optional<LevelFiles> Files);

    void Tick(float DeltaSeconds);
    bool HandleKey(const FKey& Key);

    LevelEditor& Editable() { return EditorState; }
    const LevelEditor& Editable() const { return EditorState; }
    GameMode Mode() const { return TargetMode; }
    void SetMode(GameMode Mode) { TargetMode = Mode; }

    const std::string& LevelName() const { return LevelNameValue; }
    void SetLevelName(std::string Name) { LevelNameValue = std::move(Name); }

    FEditorTool Tool() const { return CurrentTool; }
    void SetTool(FEditorTool Tool) { CurrentTool = Tool; }

    BallColor BrushColor() const { return CurrentColor; }
    void SetBrushColor(BallColor Color);
    int BrushHits() const { return CurrentHits; }
    void SetBrushHits(int Hits);
    int LauncherBalls() const { return LauncherBallCount; }
    void SetLauncherBalls(int Count);
    int SelectedBrick() const { return SelectedBrickId; }

    void SetBoardWidth(int Width);
    void SetBrickRows(int Rows);
    void SetPlayableRows(int Rows);
    void SetLauncherColumns(int Columns);
    void SetVisibleRows(int Rows);
    void AutoFillLaunchers();

    void BeginStroke(bool bErase);
    void EndStroke();
    void ApplyBrickCell(int Column, int Row, bool bErase);
    void ApplyLauncherCell(int Column, int Row, bool bErase, bool bRemoveRow);

    void Undo() { EditorState.undo(); }
    void Redo() { EditorState.redo(); }
    void Save();
    void SaveAndPlay();
    void Back();

    bool IsDirty() const { return EditorState.isDirty(); }
    bool ConfirmLeave() const { return bConfirmLeave; }
    EditorValidation Validation() const { return EditorState.validate(TargetMode); }
    LevelFiles Files() const;

    bool HasMessage() const { return MessageSecondsValue > 0.0f; }
    const std::string& Message() const { return MessageText; }
    bool MessageIsError() const { return bMessageError; }
    float MessageSeconds() const { return MessageSecondsValue; }

private:
    void NewLevel();
    bool SaveInternal();
    void ShowMessage(std::string Text, bool bError);

    FBallsApp& App;
    GameMode TargetMode = GameMode::Normal;
    LevelEditor EditorState;
    std::string LevelNameValue;
    FEditorTool CurrentTool = FEditorTool::Paint;
    BallColor CurrentColor = BallColor::Red;
    int CurrentHits = 1;
    int LauncherBallCount = 10;
    int SelectedBrickId = -1;

    int LastColumn = -1;
    int LastRow = -1;
    bool bStrokeActive = false;
    bool bDragging = false;
    bool bEraseStroke = false;
    bool bConfirmLeave = false;

    std::string MessageText;
    float MessageSecondsValue = 0.0f;
    bool bMessageError = false;
};
