#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include <optional>
#include <string>

class FBallsApp;

using FEditorTool = EditorTool;

// Front-end state for the shared LevelEditor. Now driven by the core's EditorSession: the tool, brush and launcher
// count, strokes, launcher clicks, shortcuts, steppers' ranges, name rules, saving and the unsaved-changes
// confirmation all live in the core. The Slate screen only draws it.
class FBallsMapEditor {
public:
    FBallsMapEditor(FBallsApp& App, GameMode Mode, std::optional<LevelFiles> Files);

    void Tick(float DeltaSeconds);
    bool HandleKey(const FKey& Key);

    const LevelData& Level() const { return Session.level(); }
    GameMode Mode() const { return Session.mode(); }
    void SetMode(GameMode Mode) { Session.setMode(Mode); }

    const std::string& LevelName() const { return Session.name(); }
    void SetLevelName(std::string Name) { Session.setName(Name); }

    EditorTool Tool() const { return Session.tool(); }
    void SetTool(EditorTool Tool) { Session.setTool(Tool); }

    BallColor BrushColor() const { return Session.brushColor(); }
    void SetBrushColor(BallColor Color) { Session.selectColor(Color); }
    int BrushHits() const { return Session.brushHits(); }
    void SetBrushHits(int Hits) { Session.setBrushHits(Hits); }
    int LauncherBalls() const { return Session.launcherBalls(); }
    void SetLauncherBalls(int Count) { Session.setLauncherBalls(Count); }
    int SelectedBrick() const { return Session.selectedBrick(); }

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

    void Undo() { Session.undo(); }
    void Redo() { Session.redo(); }
    void Save();
    void SaveAndPlay();
    void Back();

    bool IsDirty() const { return Session.isDirty(); }
    bool ConfirmLeave() const;
    EditorValidation Validation() const { return Session.validation(); }
    LevelFiles Files() const;

    bool HasMessage() const { return Session.message().secondsLeft > 0.0f; }
    const std::string& Message() const { return Session.message().text; }
    bool MessageIsError() const { return Session.message().isError; }
    float MessageSeconds() const { return Session.message().secondsLeft; }

    bool CanUndo() const { return Session.canUndo(); }
    bool CanRedo() const { return Session.canRedo(); }

    // Core labels, for the Slate screen.
    std::string SaveLabel() const { return Session.saveLabel(); }
    std::string_view BackLabel() const { return Session.backLabel(); }
    std::string FooterText() const { return Session.footerText(); }
    std::string SelectionText() const { return Session.selectionText(); }
    std::string ColorTallyText(BallColor Color) const { return Session.colorTallyText(Color); }
    bool ColorTallyMatches(BallColor Color) const { return Session.colorTallyMatches(Color); }

private:
    FBallsApp& App;
    EditorSession Session;
};