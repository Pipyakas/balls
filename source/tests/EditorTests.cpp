#include "TestFramework.h"

#include "editor/LevelEditor.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"

#include <filesystem>
#include <string>

namespace {

const std::filesystem::path normalLevels = std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "normal";

bool hasProblemContaining(const EditorValidation& validation, const std::string& fragment) {
    for (const std::string& problem : validation.problems) {
        if (problem.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("paint creates distinct brick ids") {
    LevelEditor editor;
    editor.newLevel("Paint", 3, 1, 1, 2, 1);
    editor.paintCell(0, 0, BallColor::Red, 1);
    editor.paintCell(1, 0, BallColor::Red, 1);
    const int first = editor.brickIdAt(0, 0);
    const int second = editor.brickIdAt(1, 0);
    EXPECT(first != emptyBrickId);
    EXPECT(second != emptyBrickId);
    EXPECT(first != second);
}

TEST_CASE("extend brick needs adjacency") {
    LevelEditor editor;
    editor.newLevel("Extend", 4, 1, 1, 1, 1);
    editor.paintCell(0, 0, BallColor::Green, 2);
    const int id = editor.brickIdAt(0, 0);
    EXPECT(!editor.extendBrick(id, 2, 0));
    EXPECT(editor.brickIdAt(2, 0) == emptyBrickId);
    EXPECT(editor.extendBrick(id, 1, 0));
    EXPECT(editor.brickIdAt(1, 0) == id);
}

TEST_CASE("erasing the middle of a brick splits it") {
    LevelEditor editor;
    editor.newLevel("Split", 3, 1, 1, 1, 1);
    editor.paintCell(0, 0, BallColor::Blue, 3);
    const int id = editor.brickIdAt(0, 0);
    EXPECT(editor.extendBrick(id, 1, 0));
    EXPECT(editor.extendBrick(id, 2, 0));
    editor.eraseCell(1, 0);
    EXPECT(editor.brickIdAt(1, 0) == emptyBrickId);
    const int left = editor.brickIdAt(0, 0);
    const int right = editor.brickIdAt(2, 0);
    EXPECT(left != emptyBrickId);
    EXPECT(right != emptyBrickId);
    EXPECT(left != right);
    const BrickType* leftType = editor.level().bricks.findType(left);
    const BrickType* rightType = editor.level().bricks.findType(right);
    EXPECT(leftType != nullptr && rightType != nullptr);
    EXPECT(leftType->color == BallColor::Blue && rightType->color == BallColor::Blue);
    EXPECT(leftType->hits == 3 && rightType->hits == 3);
}

TEST_CASE("autofill matches launcher balls to brick hits") {
    auto loaded = loadLevel(levelFilesFor(normalLevels, "DA_49Bricks"));
    EXPECT(loaded.hasValue());
    LevelEditor editor;
    editor.load(*loaded);
    editor.autoFillLaunchers(5);
    const EditorValidation validation = editor.validate(GameMode::Normal);
    EXPECT(validation.balls == validation.brickHits);
    EXPECT(!hasProblemContaining(validation, "balls for"));
}

TEST_CASE("two colors are flagged for normal mode") {
    LevelEditor editor;
    editor.newLevel("TwoColors", 3, 1, 1, 1, 1);
    editor.paintCell(0, 0, BallColor::Red, 1);
    editor.paintCell(1, 0, BallColor::Blue, 1);
    editor.setLauncher(0, 0, Launcher{BallColor::Red, 1});
    editor.setLauncher(0, 1, Launcher{BallColor::Blue, 1});
    const EditorValidation validation = editor.validate(GameMode::Normal);
    EXPECT(!validation.isPlayable());
    EXPECT(hasProblemContaining(validation, "color"));
}

TEST_CASE("editor undo and redo round trip") {
    LevelEditor editor;
    editor.newLevel("Undo", 3, 1, 1, 1, 1);
    EXPECT(!editor.canUndo());
    editor.beginEdit();
    EXPECT(editor.canUndo());
    editor.paintCell(0, 0, BallColor::Red, 1);
    EXPECT(editor.brickIdAt(0, 0) != emptyBrickId);
    EXPECT(editor.isDirty());
    EXPECT(editor.undo());
    EXPECT(editor.brickIdAt(0, 0) == emptyBrickId);
    EXPECT(editor.canRedo());
    EXPECT(editor.redo());
    EXPECT(editor.brickIdAt(0, 0) != emptyBrickId);
    editor.markSaved();
    EXPECT(!editor.isDirty());
}

TEST_CASE("editor save and reload keeps cell ids") {
    auto loaded = loadLevel(levelFilesFor(normalLevels, "DA_49Bricks"));
    EXPECT(loaded.hasValue());
    LevelEditor editor;
    editor.load(*loaded);
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "balls_editor_test";
    std::error_code error;
    std::filesystem::create_directories(tempDir, error);
    const LevelFiles tempFiles = levelFilesFor(tempDir, "DA_49Bricks");
    EXPECT(saveLevel(editor.level(), tempFiles).hasValue());
    auto reloaded = loadLevel(tempFiles);
    EXPECT(reloaded.hasValue());
    EXPECT(reloaded->bricks.cellIds == editor.level().bricks.cellIds);
    EXPECT(reloaded->bricks.width == editor.level().bricks.width);
    EXPECT(reloaded->bricks.height == editor.level().bricks.height);
}
