#include "BallsMapEditor.h"

#include "BallsApp.h"

#include "Framework/Application/SlateApplication.h"

FBallsMapEditor::FBallsMapEditor(FBallsApp& InApp, GameMode Mode, std::optional<LevelFiles> Files)
    : App(InApp), Session(Mode, Files, InApp.LevelDirectory(GameMode::Normal),
                          InApp.LevelDirectory(GameMode::Performance)) {}

void FBallsMapEditor::Tick(float DeltaSeconds) { Session.update(DeltaSeconds); }

void FBallsMapEditor::SetBoardWidth(int Width) {
    Session.setBoardSize(Width, Session.level().bricks.height, Session.level().bricks.playableHeight);
}

void FBallsMapEditor::SetBrickRows(int Rows) {
    Session.setBoardSize(Session.level().bricks.width, Rows, Session.level().bricks.playableHeight);
}

void FBallsMapEditor::SetPlayableRows(int Rows) {
    Session.setBoardSize(Session.level().bricks.width, Session.level().bricks.height, Rows);
}

void FBallsMapEditor::SetLauncherColumns(int Columns) { Session.setLauncherColumns(Columns); }

void FBallsMapEditor::SetVisibleRows(int Rows) { Session.setVisibleRows(Rows); }

void FBallsMapEditor::AutoFillLaunchers() { Session.autoFillLaunchers(); }

void FBallsMapEditor::BeginStroke(bool bErase) { Session.beginStroke(bErase); }

void FBallsMapEditor::EndStroke() { Session.endStroke(); }

void FBallsMapEditor::ApplyBrickCell(int Column, int Row, bool /*bErase*/) { Session.strokeCell(Column, Row); }

void FBallsMapEditor::ApplyLauncherCell(int Column, int Row, bool bErase, bool bRemoveRow) {
    LauncherClick Click = LauncherClick::Set;
    if (bRemoveRow) {
        Click = LauncherClick::DeleteRow;
    } else if (bErase) {
        Click = LauncherClick::Clear;
    }
    Session.clickLauncher(Column, Row, Click);
}

bool FBallsMapEditor::HandleKey(const FKey& Key) {
    const bool bControl = FSlateApplication::Get().GetModifierKeys().IsControlDown();
    if (bControl) {
        if (Key == EKeys::Z) {
            return Session.handleShortcut(EditorShortcut::Undo);
        }
        if (Key == EKeys::Y) {
            return Session.handleShortcut(EditorShortcut::Redo);
        }
        if (Key == EKeys::S) {
            return Session.handleShortcut(EditorShortcut::Save);
        }
        return false;
    }

    if (Key == EKeys::P) {
        return Session.handleShortcut(EditorShortcut::Paint);
    }
    if (Key == EKeys::E) {
        return Session.handleShortcut(EditorShortcut::Extend);
    }
    if (Key == EKeys::X) {
        return Session.handleShortcut(EditorShortcut::Erase);
    }
    if (Key == EKeys::K) {
        return Session.handleShortcut(EditorShortcut::Pick);
    }
    const FKey Digits[ballColorCount] = {EKeys::One, EKeys::Two,   EKeys::Three, EKeys::Four,
                                         EKeys::Five, EKeys::Six, EKeys::Seven};
    for (int Color = 0; Color < ballColorCount; ++Color) {
        if (Key == Digits[Color]) {
            return Session.handleShortcut(static_cast<EditorShortcut>(
                static_cast<int>(EditorShortcut::Color1) + Color));
        }
    }
    if (Key == EKeys::Escape) {
        Back();
        return true;
    }
    return false;
}

LevelFiles FBallsMapEditor::Files() const {
    return levelFilesFor(App.LevelDirectory(Session.mode()), Session.name());
}

void FBallsMapEditor::Save() { Session.save(); }

void FBallsMapEditor::SaveAndPlay() {
    if (auto Files = Session.saveAndPlay()) {
        App.GetScreenFlow().playFromEditor(Session.mode(), *Files);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
    }
}

void FBallsMapEditor::Back() {
    if (Session.back() == EditorLeave::Confirm) {
        return;
    }
    App.GetScreenFlow().activate(FlowAction::Back);
    App.ProcessFlowRequests();
    App.ApplyFlowScreen();
}

bool FBallsMapEditor::ConfirmLeave() const { return Session.isConfirmingLeave(); }