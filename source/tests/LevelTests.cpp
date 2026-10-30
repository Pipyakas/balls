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

TEST_CASE("parser reports malformed input") {
    EXPECT(!parseBrickLayout("[Board]\nWidth 2\nHeight 1\nPlayableHeight 3\n[Layout]\n0\n[BrickTypes]\n").hasValue());
    EXPECT(!parseLauncherLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\nPink(3)\n").hasValue());
    EXPECT(!parseLauncherLayout("[Board]\nWidth 1\nHeight 1\n[Layout]\nRed(0)\n").hasValue());
}

TEST_CASE("a board too large for the simulation's 32-bit cell indexes is refused with a message") {
    const auto layout = parseBrickLayout("[Board]\nWidth 20000\nHeight 20000\nPlayableHeight 25000\n[Layout]\n");
    EXPECT(!layout.hasValue());
    EXPECT(layout.error().find("too large") != std::string::npos);
}
