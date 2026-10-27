#include "BallsMapEditor.h"

#include "BallsApp.h"

#include "Framework/Application/SlateApplication.h"

#include <algorithm>

namespace {

constexpr int MaximumBoardSize = 200;
constexpr int MaximumHits = 999;
constexpr int MaximumLauncherBalls = 100000;
constexpr float MessageLifetime = 3.0f;

}

FBallsMapEditor::FBallsMapEditor(FBallsApp& InApp, GameMode Mode, std::optional<LevelFiles> Files) : App(InApp) {
    TargetMode = Mode;
    if (Files) {
        if (auto Loaded = loadLevel(*Files); Loaded) {
            EditorState.load(std::move(*Loaded));
            LevelNameValue = Files->name;
            EditorState.markSaved();
            return;
        } else {
            ShowMessage("Could not load: " + Loaded.error(), true);
        }
    }
    NewLevel();
}

void FBallsMapEditor::NewLevel() {
    LevelNameValue = "NewLevel";
    EditorState.newLevel(LevelNameValue, 10, 8, 20, 3, 3);
    SelectedBrickId = -1;
}

void FBallsMapEditor::ShowMessage(std::string Text, bool bError) {
    MessageText = std::move(Text);
    MessageSecondsValue = MessageLifetime;
    bMessageError = bError;
}

void FBallsMapEditor::Tick(float DeltaSeconds) {
    MessageSecondsValue = FMath::Max(0.0f, MessageSecondsValue - DeltaSeconds);
}

void FBallsMapEditor::SetBrushColor(BallColor Color) {
    CurrentColor = Color;
    if (CurrentTool == FEditorTool::Pick && SelectedBrickId >= 0) {
        EditorState.beginEdit();
        EditorState.setBrickColor(SelectedBrickId, Color);
    }
}

void FBallsMapEditor::SetBrushHits(int Hits) {
    CurrentHits = FMath::Clamp(Hits, 1, MaximumHits);
    if (CurrentTool == FEditorTool::Pick && SelectedBrickId >= 0) {
        EditorState.beginEdit();
        EditorState.setBrickHits(SelectedBrickId, CurrentHits);
    }
}

void FBallsMapEditor::SetLauncherBalls(int Count) {
    LauncherBallCount = FMath::Clamp(Count, 1, MaximumLauncherBalls);
}

void FBallsMapEditor::SetBoardWidth(int Width) {
    const BrickLayout& Bricks = EditorState.level().bricks;
    EditorState.beginEdit();
    EditorState.resizeBoard(FMath::Clamp(Width, 1, MaximumBoardSize), Bricks.height,
                            FMath::Max(Bricks.playableHeight, Bricks.height));
}

void FBallsMapEditor::SetBrickRows(int Rows) {
    const BrickLayout& Bricks = EditorState.level().bricks;
    const int NewRows = FMath::Clamp(Rows, 1, MaximumBoardSize);
    EditorState.beginEdit();
    EditorState.resizeBoard(Bricks.width, NewRows, FMath::Max(Bricks.playableHeight, NewRows));
}

void FBallsMapEditor::SetPlayableRows(int Rows) {
    const BrickLayout& Bricks = EditorState.level().bricks;
    EditorState.beginEdit();
    EditorState.resizeBoard(Bricks.width, Bricks.height,
                            FMath::Max(FMath::Clamp(Rows, Bricks.height, MaximumBoardSize * 2), Bricks.height));
}

void FBallsMapEditor::SetLauncherColumns(int Columns) {
    EditorState.beginEdit();
    EditorState.setLauncherColumns(FMath::Clamp(Columns, 1, 8));
}

void FBallsMapEditor::SetVisibleRows(int Rows) {
    EditorState.beginEdit();
    EditorState.setVisibleRows(FMath::Clamp(Rows, 1, 8));
}

void FBallsMapEditor::AutoFillLaunchers() {
    EditorState.beginEdit();
    EditorState.autoFillLaunchers(LauncherBallCount > 0 ? LauncherBallCount : 10);
}

void FBallsMapEditor::BeginStroke(bool bErase) {
    EditorState.beginEdit();
    bStrokeActive = true;
    bEraseStroke = bErase;
    bDragging = false;
    LastColumn = -1;
    LastRow = -1;
}

void FBallsMapEditor::EndStroke() {
    bStrokeActive = false;
    bDragging = false;
    LastColumn = -1;
    LastRow = -1;
}

void FBallsMapEditor::ApplyBrickCell(int Column, int Row, bool bErase) {
    if (!bStrokeActive) {
        return;
    }
    const BrickLayout& Bricks = EditorState.level().bricks;
    if (Column < 0 || Row < 0 || Column >= Bricks.width || Row >= Bricks.height) {
        return;
    }
    if (Column == LastColumn && Row == LastRow) {
        return;
    }

    if (bErase || CurrentTool == FEditorTool::Erase) {
        EditorState.eraseCell(Column, Row);
    } else {
        const int Under = EditorState.brickIdAt(Column, Row);
        switch (CurrentTool) {
        case FEditorTool::Paint:
            EditorState.paintCell(Column, Row, CurrentColor, CurrentHits);
            break;
        case FEditorTool::Extend:
            if (!bDragging && Under != emptyBrickId) {
                SelectedBrickId = Under;
            } else if (SelectedBrickId >= 0) {
                EditorState.extendBrick(SelectedBrickId, Column, Row);
            }
            break;
        case FEditorTool::Pick:
            SelectedBrickId = Under;
            if (const BrickType* Type = EditorState.level().bricks.findType(Under)) {
                CurrentColor = Type->color;
                CurrentHits = Type->hits;
            }
            break;
        case FEditorTool::Erase:
            break;
        }
    }

    bDragging = (LastColumn >= 0) || CurrentTool != FEditorTool::Extend || SelectedBrickId >= 0;
    LastColumn = Column;
    LastRow = Row;
    (void)bEraseStroke;
}

void FBallsMapEditor::ApplyLauncherCell(int Column, int Row, bool bErase, bool bRemoveRow) {
    const LauncherLayout& Launchers = EditorState.level().launchers;
    if (Column < 0 || Row < 0 || Column >= Launchers.columns) {
        return;
    }
    EditorState.beginEdit();
    if (bRemoveRow) {
        if (Row < Launchers.rowCount()) {
            EditorState.removeLauncherRow(Row);
        }
        return;
    }
    if (bErase) {
        if (Row < Launchers.rowCount()) {
            EditorState.setLauncher(Column, Row, std::nullopt);
        }
        return;
    }
    EditorState.setLauncher(Column, Row, Launcher{CurrentColor, LauncherBallCount});
}

bool FBallsMapEditor::HandleKey(const FKey& Key) {
    const bool bControl = FSlateApplication::Get().GetModifierKeys().IsControlDown();
    if (bControl) {
        if (Key == EKeys::Z) {
            Undo();
            return true;
        }
        if (Key == EKeys::Y) {
            Redo();
            return true;
        }
        if (Key == EKeys::S) {
            Save();
            return true;
        }
        return false;
    }

    if (Key == EKeys::P) {
        CurrentTool = FEditorTool::Paint;
        return true;
    }
    if (Key == EKeys::E) {
        CurrentTool = FEditorTool::Extend;
        return true;
    }
    if (Key == EKeys::X) {
        CurrentTool = FEditorTool::Erase;
        return true;
    }
    if (Key == EKeys::K) {
        CurrentTool = FEditorTool::Pick;
        return true;
    }
    const FKey Digits[ballColorCount] = {EKeys::One, EKeys::Two,   EKeys::Three, EKeys::Four,
                                         EKeys::Five, EKeys::Six, EKeys::Seven};
    for (int Color = 0; Color < ballColorCount; ++Color) {
        if (Key == Digits[Color]) {
            SetBrushColor(ballColorFromIndex(Color));
            return true;
        }
    }
    if (Key == EKeys::Escape) {
        Back();
        return true;
    }
    return false;
}

LevelFiles FBallsMapEditor::Files() const {
    return levelFilesFor(App.LevelDirectory(TargetMode), LevelNameValue);
}

bool FBallsMapEditor::SaveInternal() {
    if (LevelNameValue.empty()) {
        ShowMessage("Enter a level name first", true);
        return false;
    }
    LevelData Level = EditorState.level();
    Level.name = LevelNameValue;
    if (auto Result = saveLevel(Level, Files()); !Result) {
        ShowMessage(Result.error(), true);
        return false;
    }
    EditorState.markSaved();
    ShowMessage("Saved " + LevelNameValue + (TargetMode == GameMode::Normal ? " (normal)" : " (performance)"), false);
    return true;
}

void FBallsMapEditor::Save() {
    SaveInternal();
}

void FBallsMapEditor::SaveAndPlay() {
    if (SaveInternal()) {
        App.StartGame(TargetMode, Files());
    }
}

void FBallsMapEditor::Back() {
    if (EditorState.isDirty() && !bConfirmLeave) {
        bConfirmLeave = true;
        ShowMessage("Unsaved changes - press Back again to discard", true);
        return;
    }
    App.ShowMainMenu();
}
