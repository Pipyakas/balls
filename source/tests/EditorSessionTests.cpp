#include "TestFramework.h"

#include "editor/EditorSession.h"
#include "level/LevelRepository.h"

#include <filesystem>

namespace {

struct EditorFolders {
    std::filesystem::path root;
    std::filesystem::path normal;
    std::filesystem::path performance;

    explicit EditorFolders(const char* name)
        : root(std::filesystem::temp_directory_path() / name), normal(root / "normal"),
          performance(root / "performance") {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(normal);
        std::filesystem::create_directories(performance);
    }
    ~EditorFolders() { std::filesystem::remove_all(root); }
};

}

TEST_CASE("a new editor level follows the design defaults") {
    EditorFolders folders("balls_editor_session_new");
    const EditorSession session(GameMode::Normal, std::nullopt, folders.normal, folders.performance);
    EXPECT(session.name() == "NewLevel");
    EXPECT(session.level().bricks.width == 10);
    EXPECT(session.level().bricks.height == 8);
    EXPECT(session.level().bricks.playableHeight == 20);
    EXPECT(session.level().launchers.columns == 3);
    EXPECT(session.level().launchers.visibleRows == 3);
    EXPECT(session.tool() == EditorTool::Paint);
    EXPECT(session.selectionText() == "No brick selected");
    EXPECT(session.backLabel() == "Back");
    EXPECT(session.saveLabel() == "Save  (Ctrl+S)");
    EXPECT(EditorSession::toolButtons().size() == 4);
}

TEST_CASE("an editor stroke is one undo step and right-drag erases") {
    EditorFolders folders("balls_editor_session_stroke");
    EditorSession session(GameMode::Normal, std::nullopt, folders.normal, folders.performance);
    session.beginStroke(false);
    session.strokeCell(0, 0);
    session.strokeCell(0, 0);
    session.strokeCell(1, 0);
    session.endStroke();
    EXPECT(session.level().bricks.findType(session.level().bricks.cellIds[0]) != nullptr);
    EXPECT(session.isDirty());
    EXPECT(session.saveLabel() == "Save *  (Ctrl+S)");

    session.beginStroke(true);
    session.strokeCell(0, 0);
    session.endStroke();
    EXPECT(session.level().bricks.cellIds[0] == emptyBrickId);

    session.undo();
    EXPECT(session.level().bricks.cellIds[0] != emptyBrickId);
    session.undo();
    EXPECT(session.level().bricks.cellIds[0] == emptyBrickId);
    EXPECT(session.level().bricks.cellIds[1] == emptyBrickId);
    EXPECT(!session.canUndo());
}

TEST_CASE("the pick tool reads a brick and recolors it, the extend tool grows it") {
    EditorFolders folders("balls_editor_session_pick");
    EditorSession session(GameMode::Normal, std::nullopt, folders.normal, folders.performance);
    session.setBrushHits(3);
    session.selectColor(BallColor::Blue);
    session.beginStroke(false);
    session.strokeCell(2, 2);
    session.endStroke();

    session.setBrushHits(1);
    session.selectColor(BallColor::Red);
    EXPECT(session.handleShortcut(EditorShortcut::Pick));
    session.beginStroke(false);
    session.strokeCell(2, 2);
    session.endStroke();
    EXPECT(session.brushColor() == BallColor::Blue);
    EXPECT(session.brushHits() == 3);
    const int picked = session.selectedBrick();
    EXPECT(picked != emptyBrickId);
    EXPECT(session.selectionText() == "Selected id " + std::to_string(picked));
    EXPECT(session.handleShortcut(EditorShortcut::Color5));
    EXPECT(session.level().bricks.findType(picked)->color == ballColorFromIndex(4));

    session.setTool(EditorTool::Extend);
    session.beginStroke(false);
    session.strokeCell(2, 2);
    session.strokeCell(3, 2);
    session.endStroke();
    EXPECT(session.level().bricks.cellIds[static_cast<size_t>(2 * session.level().bricks.width + 3)] == picked);
}

TEST_CASE("Back asks once about unsaved changes, and another edit asks again") {
    EditorFolders folders("balls_editor_session_back");
    EditorSession session(GameMode::Normal, std::nullopt, folders.normal, folders.performance);
    EXPECT(session.back() == EditorLeave::Leave);

    session.beginStroke(false);
    session.strokeCell(0, 0);
    session.endStroke();
    EXPECT(session.back() == EditorLeave::Confirm);
    EXPECT(session.backLabel() == "Discard changes?");
    session.setTool(EditorTool::Erase);
    EXPECT(session.backLabel() == "Back");
    EXPECT(session.back() == EditorLeave::Confirm);
    EXPECT(session.back() == EditorLeave::Leave);
}

TEST_CASE("the name keeps file-safe characters, saving writes the pair and Save & Play needs a valid level") {
    EditorFolders folders("balls_editor_session_save");
    EditorSession session(GameMode::Normal, std::nullopt, folders.normal, folders.performance);
    session.setName("My Level/1!_x-y");
    EXPECT(session.name() == "MyLevel1_x-y");
    session.setName(std::string(60, 'a'));
    EXPECT(session.name().size() == editorMaximumNameLength);
    session.setName("");
    EXPECT(!session.save());
    EXPECT(session.message().text == "Enter a level name first");
    EXPECT(session.message().isError);

    session.setName("Saved");

    for (int column = 0; column < 3; ++column) {
        session.selectColor(ballColorFromIndex(column));
        session.beginStroke(false);
        session.strokeCell(column, 0);
        session.endStroke();
    }
    EXPECT(!session.validation().isPlayable());
    EXPECT(!session.saveAndPlay());

    session.autoFillLaunchers();
    EXPECT(session.validation().isPlayable());
    EXPECT(session.colorTallyMatches(BallColor::Red));
    const auto files = session.saveAndPlay();
    EXPECT(files.has_value());
    EXPECT(std::filesystem::exists(files->bricksPath));
    EXPECT(session.message().text == "Saved Saved (normal)");
    EXPECT(!session.isDirty());

    const EditorSession reopened(GameMode::Normal, *files, folders.normal, folders.performance);
    EXPECT(reopened.name() == "Saved");
    EXPECT(!reopened.isDirty());
}

TEST_CASE("editor ranges clamp to the game's limits and launcher clicks set, clear and delete rows") {
    EditorFolders folders("balls_editor_session_ranges");
    EditorSession session(GameMode::Performance, std::nullopt, folders.normal, folders.performance);
    session.setBoardSize(500, 0, 1000);
    EXPECT(session.level().bricks.width == editorMaximumBoardSize);
    EXPECT(session.level().bricks.height == 1);
    EXPECT(session.level().bricks.playableHeight == editorMaximumTotalRows);
    session.setBrushHits(5000);
    EXPECT(session.brushHits() == editorMaximumHits);
    session.setLauncherBalls(0);
    EXPECT(session.launcherBalls() == 1);
    session.setLauncherColumns(20);
    EXPECT(session.level().launchers.columns == editorMaximumLauncherColumns);

    const int rows = session.level().launchers.rowCount();
    session.clickLauncher(0, rows, LauncherClick::Set);
    EXPECT(session.level().launchers.rowCount() == rows + 1);
    EXPECT(session.level().launchers.slot(0, rows).has_value());
    session.clickLauncher(0, rows, LauncherClick::Clear);
    EXPECT(!session.level().launchers.slot(0, rows).has_value());
    session.clickLauncher(0, rows, LauncherClick::DeleteRow);
    EXPECT(session.level().launchers.rowCount() == rows);
    session.clickLauncher(0, rows + 3, LauncherClick::Clear);
    EXPECT(session.level().launchers.rowCount() == rows);
}

TEST_CASE("a level that fails to load opens a new one and says why") {
    EditorFolders folders("balls_editor_session_missing");
    const EditorSession session(GameMode::Normal, levelFilesFor(folders.normal, "Missing"), folders.normal,
                                folders.performance);
    EXPECT(session.name() == "NewLevel");
    EXPECT(session.message().isError);
    EXPECT(session.message().text.rfind("Could not load: ", 0) == 0);
}
