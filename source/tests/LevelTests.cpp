#include "TestFramework.h"

#include "common/BallCount.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"

#include <filesystem>

namespace {

const std::filesystem::path normalLevels = std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "normal";
const std::filesystem::path performanceLevels = std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "performance";

}

TEST_CASE("repository pairs brick and spawn files") {
    const auto levels = findLevels(normalLevels);
    EXPECT(levels.size() >= 2);
    for (const auto& level : levels) {
        EXPECT(std::filesystem::exists(level.launchersPath));
    }
}

TEST_CASE("sample levels parse with matching ball and hit totals") {
    for (const auto& directory : {normalLevels, performanceLevels}) {
        for (const auto& files : findLevels(directory)) {
            auto level = loadLevel(files);
            EXPECT(level.hasValue());
            const ColorTally hits = countBrickHitsByColor(level->bricks);
            const ColorTally balls = countBallsByColor(level->launchers);
            for (int color = 0; color < ballColorCount; ++color) {
                EXPECT(balls[color] >= hits[color]);
            }
        }
    }
}

TEST_CASE("every shipped level but the samples' names its brick count") {
    for (const auto& directory : {normalLevels, performanceLevels}) {
        for (const auto& files : findLevels(directory)) {
            if (files.name.starts_with("DA_")) {
                continue;
            }
            auto level = loadLevel(files);
            EXPECT(level.hasValue());
            const std::string suffix = levelNameWithBrickCount("", countPlacedBricks(level->bricks));
            EXPECT(files.name.ends_with(suffix));
        }
    }
}

TEST_CASE("the Unlimited level holds more balls than any machine's capacity, even at x1") {
    constexpr BallCount largestCapacity = BallCount{10'000'000'000};
    auto level = loadLevel(levelFilesFor(performanceLevels, "Unlimited_921Bricks"));
    EXPECT(level.hasValue());
    BallCount total = 0;
    for (const int balls : countBallsByColor(level->launchers)) {
        total += static_cast<BallCount>(balls);
    }
    EXPECT(total > largestCapacity);
}

TEST_CASE("the team's Normal levels release exactly one ball per brick hit from 3 launcher columns") {
    constexpr int launcherColumns = 3;
    for (const char* name : {"Warmup_24Bricks", "Pillars_28Bricks", "Stalactites_33Bricks", "Fortress_43Bricks"}) {
        auto level = loadLevel(levelFilesFor(normalLevels, name));
        EXPECT(level.hasValue());
        EXPECT(level->launchers.columns == launcherColumns);
        const ColorTally hits = countBrickHitsByColor(level->bricks);
        const ColorTally balls = countBallsByColor(level->launchers);
        int colors = 0;
        for (int color = 0; color < ballColorCount; ++color) {
            EXPECT(balls[color] == hits[color]);
            colors += hits[color] > 0 ? 1 : 0;
        }
        EXPECT(colors >= 3);
    }
}

TEST_CASE("multi-cell bricks share one id") {
    auto level = loadLevel(levelFilesFor(normalLevels, "DA_49Bricks"));
    EXPECT(level.hasValue());
    EXPECT(level->bricks.cellId(4, 0) == 4);
    EXPECT(level->bricks.cellId(5, 1) == 4);
    EXPECT(level->bricks.findType(31)->hits == 10);
    EXPECT(level->launchers.columns == 3);
    EXPECT(!level->launchers.slot(1, 5).has_value());
}

TEST_CASE("writer output parses back to the same level") {
    auto level = loadLevel(levelFilesFor(normalLevels, "DA_49Bricks"));
    EXPECT(level.hasValue());
    auto bricks = parseBrickLayout(formatBrickLayout(level->bricks));
    auto launchers = parseLauncherLayout(formatLauncherLayout(level->launchers));
    EXPECT(bricks.hasValue() && launchers.hasValue());
    EXPECT(bricks->cellIds == level->bricks.cellIds);
    EXPECT(bricks->playableHeight == level->bricks.playableHeight);
    EXPECT(countBrickHitsByColor(*bricks) == countBrickHitsByColor(level->bricks));
    EXPECT(countBallsByColor(*launchers) == countBallsByColor(level->launchers));
}

TEST_CASE("parser reports input it cannot repair") {
    EXPECT(!parseBrickLayout("[Layout]\n0\n[BrickTypes]\n0 Red 1\n").hasValue());
    EXPECT(!parseBrickLayout("[Board]\nHeight 1\n[Layout]\n0\n[BrickTypes]\n0 Red 1\n").hasValue());
    EXPECT(!parseBrickLayout("[Board]\nWidth 1\nHeight 1\n[BrickTypes]\n0 Red 1\n").hasValue());
    EXPECT(!parseBrickLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\nabc\n").hasValue());
    EXPECT(!parseLauncherLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\nPink(3)\n").hasValue());
    EXPECT(!parseLauncherLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\nRed\n").hasValue());
    EXPECT(!parseLauncherLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\n").hasValue());
}

TEST_CASE("parser repairs header and section slips") {
    const auto bricks = parseBrickLayout("\xEF\xBB\xBF// made by hand\n[board]\nwidth: 2\nHEIGHT = 1\nPlayableHeight=4\n"
                                         "Author someone else\n[LAYOUT]\n# the only row\n0 1\n[bricktypes]\n0 red 2\n"
                                         "1 Blue 3 extra\n");
    EXPECT(bricks.hasValue());
    EXPECT(bricks->width == 2 && bricks->height == 1 && bricks->playableHeight == 4);
    EXPECT(bricks->cellIds == std::vector<int>({0, 1}));
    EXPECT(bricks->types.size() == 2 && bricks->findType(1)->hits == 3);
}

TEST_CASE("parser takes the layout rows as written when they disagree with the header") {
    const auto bricks =
        parseBrickLayout("[Board]\nWidth 2\nHeight 1\nPlayableHeight 1\n[Layout]\n0\n1 1 2\n[BrickTypes]\n0 Red 1\n"
                         "1 Red 1\n2 Red 1\n");
    EXPECT(bricks.hasValue());
    EXPECT(bricks->width == 3 && bricks->height == 2 && bricks->playableHeight == 9);
    EXPECT(bricks->cellIds == std::vector<int>({0, emptyBrickId, emptyBrickId, 1, 1, 2}));
}

TEST_CASE("parser keeps the readable brick types and the first of a repeated id") {
    const auto bricks = parseBrickLayout("[Board]\nWidth 4\nHeight 1\n[Layout]\n0 1 2 x\n[BrickTypes]\n0 Red 1\n"
                                         "1 Pink 2\n2 Green 0\n0 Blue 5\n");
    EXPECT(bricks.hasValue());
    EXPECT(bricks->types.size() == 1 && bricks->findType(0)->hits == 1);
    EXPECT(bricks->cellIds == std::vector<int>({0, emptyBrickId, emptyBrickId, emptyBrickId}));
    const auto noTypes = parseBrickLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\n0\n");
    EXPECT(noTypes.hasValue() && noTypes->types.empty() && noTypes->cellIds[0] == emptyBrickId);
}

TEST_CASE("parser repairs launcher rows") {
    const auto launchers = parseLauncherLayout("[Board]\nWidth 2\n[Layout]\nRed (3) blue(2) Green(1)\nempty\n"
                                               "Yellow(0) Red(4)\n");
    EXPECT(launchers.hasValue());
    EXPECT(launchers->columns == 3 && launchers->visibleRows == 3 && launchers->rowCount() == 3);
    EXPECT(launchers->slot(0, 0)->color == BallColor::Red && launchers->slot(0, 0)->ballCount == 3);
    EXPECT(launchers->slot(1, 0)->color == BallColor::Blue);
    EXPECT(!launchers->slot(0, 1).has_value() && !launchers->slot(2, 1).has_value());
    EXPECT(!launchers->slot(0, 2).has_value() && launchers->slot(1, 2)->ballCount == 4);
}

TEST_CASE("a board too large for the simulation's 32-bit cell indexes is refused with a message") {
    const auto layout = parseBrickLayout("[Board]\nWidth 20000\nHeight 20000\nPlayableHeight 25000\n[Layout]\n");
    EXPECT(!layout.hasValue());
    EXPECT(layout.error().find("too large") != std::string::npos);
}

TEST_CASE("a board without PlayableHeight gets open rows below its bricks") {
    const auto small = parseBrickLayout("[Board]\nWidth 1\nHeight 2\n[Layout]\n0\n0\n[BrickTypes]\n0 Red 1\n");
    EXPECT(small.hasValue() && small->playableHeight == 9);
    std::string tall = "[Board]\nWidth 1\nHeight 128\n[Layout]\n";
    for (int row = 0; row < 128; ++row) {
        tall += "-1\n";
    }
    const auto large = parseBrickLayout(tall + "[BrickTypes]\n");
    EXPECT(large.hasValue() && large->playableHeight == 160);
}
