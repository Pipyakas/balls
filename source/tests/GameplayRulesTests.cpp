#include "TestFramework.h"

#include "level/GameplayRules.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <filesystem>

namespace {

const std::filesystem::path levelsFolder = std::filesystem::path(BALLS_ASSET_DIR) / "levels";

}

TEST_CASE("gameplay rules read every key and keep the built-in value for the ones left out") {
    const auto rules = parseGameplayRules("# a comment\n[Balls]\nSpeed 10\nRetargetAfterBounces 2\n[Normal]\nSpawnRate 30\n");
    EXPECT(rules.hasValue());
    EXPECT(rules->ballSpeed == 10.0f);
    EXPECT(rules->retargetBounces == 2);
    EXPECT(rules->normalSpawnRate == 30.0f);
    EXPECT(rules->ballRadius == GameplayRules{}.ballRadius);
    EXPECT(rules->ticksPerSecond == GameplayRules{}.ticksPerSecond);
}

TEST_CASE("gameplay rules reject values the simulation cannot run") {
    EXPECT(!parseGameplayRules("[Balls]\nRadius 0.6\n").hasValue());
    EXPECT(!parseGameplayRules("[Balls]\nRetargetAfterBounces 4\n").hasValue());
    EXPECT(!parseGameplayRules("[Balls]\nSpeed fast\n").hasValue());
    EXPECT(!parseGameplayRules("[Balls]\nSpeed 40\n").hasValue());
    EXPECT(!parseGameplayRules("[Simulation]\nTicksPerSecond 10\n").hasValue());
}

TEST_CASE("every bundled level plays by the shipped Gameplay.txt") {
    const auto shipped = readTextFile(levelsFolder / gameplayRulesFileName);
    EXPECT(shipped.hasValue());
    const auto rules = parseGameplayRules(*shipped);
    EXPECT(rules.hasValue());
    auto level = loadLevel(levelFilesFor(levelsFolder / "normal", "DA_FirstLevel"));
    EXPECT(level.hasValue());
    EXPECT(level->rules.ballSpeed == rules->ballSpeed);
    EXPECT(level->rules.retargetBounces == rules->retargetBounces);
    EXPECT(level->rules.normalSpawnRate == rules->normalSpawnRate);
}
