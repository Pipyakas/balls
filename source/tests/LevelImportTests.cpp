#include "TestFramework.h"

#include "level/LevelImporter.h"
#include "level/LevelRepository.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr const char* twoRedBricks = "[Board]\nWidth 2\nHeight 1\nPlayableHeight 4\n[Layout]\n0 1\n[BrickTypes]\n0 Red 1\n1 Red 1\n";
constexpr const char* twoRedBalls = "[Board]\nWidth 1\nHeight 1\n[Layout]\nRed(2)\n";
constexpr const char* oneRedBall = "[Board]\nWidth 1\nHeight 1\n[Layout]\nRed(1)\n";
constexpr const char* brokenBricks = "[Board]\nWidth 2\nHeight 1\nPlayableHeight 4\n[Layout]\n0\n[BrickTypes]\n0 Red 1\n";

// A scratch folder per test case, removed again when the test ends. The random suffix keeps test runs from other
// worktrees, which share the temp folder, from deleting each other's files.
std::string runSuffix() {
    static const std::string suffix = std::to_string(std::random_device{}());
    return suffix;
}

class ScratchFolder {
public:
    explicit ScratchFolder(const std::string& name)
        : root(std::filesystem::temp_directory_path() / ("balls-import-test-" + runSuffix() + "-" + name)) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "picked");
    }
    ~ScratchFolder() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path picked() const { return root / "picked"; }
    std::filesystem::path levels() const { return root / "levels"; }

    std::filesystem::path write(const std::string& fileName, const char* text) const {
        const std::filesystem::path path = picked() / fileName;
        std::ofstream(path, std::ios::binary) << text;
        return path;
    }

private:
    std::filesystem::path root;
};

LevelImportReport importPaths(const ScratchFolder& folder, std::vector<std::filesystem::path> paths,
                              GameMode mode = GameMode::Performance) {
    return importLevels(paths, folder.levels(), mode);
}

}

TEST_CASE("importing either file of a pair copies both into the mode folder") {
    const ScratchFolder folder("pair");
    folder.write("Arena_Bricks.txt", twoRedBricks);
    const auto spawn = folder.write("Arena_SpawnBall.txt", twoRedBalls);
    const LevelImportReport report = importPaths(folder, {spawn});
    EXPECT(report.summary() == "Imported Arena");
    EXPECT(report.levelToHighlight() == std::optional<std::string>("Arena"));
    const auto levels = findLevels(folder.levels());
    EXPECT(levels.size() == 1);
    EXPECT(levels.front().name == "Arena");
}

TEST_CASE("importing a pair with an existing name replaces it") {
    const ScratchFolder folder("replace");
    const auto bricks = folder.write("Arena_Bricks.txt", twoRedBricks);
    folder.write("Arena_SpawnBall.txt", twoRedBalls);
    importPaths(folder, {bricks});
    const LevelImportReport report = importPaths(folder, {bricks});
    EXPECT(report.summary() == "Replaced Arena");
    EXPECT(!report.anyFailed());
}

TEST_CASE("a file without its partner is rejected and nothing is copied") {
    const ScratchFolder folder("partner");
    const auto bricks = folder.write("Lonely_Bricks.txt", twoRedBricks);
    const LevelImportReport report = importPaths(folder, {bricks});
    EXPECT(report.summary() == "Import failed: Lonely_SpawnBall.txt is missing");
    EXPECT(report.anyFailed());
    EXPECT(findLevels(folder.levels()).empty());
}

TEST_CASE("a pair that does not parse names the file and is not copied") {
    const ScratchFolder folder("broken");
    const auto bricks = folder.write("Broken_Bricks.txt", brokenBricks);
    folder.write("Broken_SpawnBall.txt", twoRedBalls);
    const LevelImportReport report = importPaths(folder, {bricks});
    EXPECT(report.summary().starts_with("Import failed: Broken_Bricks.txt: "));
    EXPECT(!std::filesystem::exists(folder.levels() / "Broken_Bricks.txt"));
}

TEST_CASE("normal-mode ball shortfalls are imported with a warning") {
    const ScratchFolder folder("warning");
    const auto bricks = folder.write("Short_Bricks.txt", twoRedBricks);
    folder.write("Short_SpawnBall.txt", oneRedBall);
    const LevelImportReport report = importPaths(folder, {bricks}, GameMode::Normal);
    EXPECT(report.summary() == "Imported Short - warning: Red balls 1 / hits 2");
    EXPECT(!report.anyFailed());
    EXPECT(findLevels(folder.levels()).size() == 1);
    EXPECT(importPaths(folder, {bricks}, GameMode::Performance).summary() == "Replaced Short");
}

TEST_CASE("a dropped folder imports every pair in it with one summary") {
    const ScratchFolder folder("folder");
    for (const char* name : {"First", "Second", "Third"}) {
        folder.write(std::string(name) + "_Bricks.txt", twoRedBricks);
        folder.write(std::string(name) + "_SpawnBall.txt", twoRedBalls);
    }
    const LevelImportReport report = importPaths(folder, {folder.picked()});
    EXPECT(report.summary() == "Imported 3 levels");
    EXPECT(findLevels(folder.levels()).size() == 3);
}

TEST_CASE("a file that is not a level file is reported by name") {
    const ScratchFolder folder("stranger");
    const auto notes = folder.write("notes.txt", "hello");
    const LevelImportReport report = importPaths(folder, {notes});
    EXPECT(report.summary().starts_with("Import failed: notes.txt: not a level file"));
}
