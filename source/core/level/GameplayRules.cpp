#include "level/GameplayRules.h"

#include "level/SectionText.h"

#include <charconv>
#include <format>
#include <string>

namespace {

constexpr int mostCountedBounces = 3;
constexpr int fewestTicksPerSecond = 30;
constexpr int mostTicksPerSecond = 1000;
constexpr float largestBallRadius = 0.5f;
constexpr float widestLaunchHalfAngle = 90.0f;

template <typename Number>
Result<Number> parseNumber(std::string_view token) {
    Number value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size()) {
        return Error{std::format("'{}' is not a number", token)};
    }
    return value;
}

struct RuleKey {
    std::string_view section;
    std::string_view key;
};

// Reads one "Key Value" line of a section into target; a section or key the file leaves out keeps its value.
template <typename Number>
Result<bool> readRule(const SectionMap& sections, RuleKey where, Number& target) {
    const auto section = sections.find(where.section);
    if (section == sections.end()) {
        return false;
    }
    for (const std::string_view line : section->second) {
        const std::vector<std::string_view> tokens = splitTokens(line);
        if (tokens.empty() || tokens.front() != where.key) {
            continue;
        }
        if (tokens.size() != 2) {
            return Error{std::format("[{}] {} needs one value", where.section, where.key)};
        }
        auto value = parseNumber<Number>(tokens[1]);
        if (!value) {
            return Error{std::format("[{}] {}: {}", where.section, where.key, value.error())};
        }
        target = *value;
        return true;
    }
    return false;
}

std::string validate(const GameplayRules& rules) {
    if (rules.ballRadius <= 0.0f || rules.ballRadius >= largestBallRadius) {
        return std::format("[Balls] Radius must be above 0 and below {} cells", largestBallRadius);
    }
    if (rules.ballSpeed <= 0.0f) {
        return "[Balls] Speed must be above 0";
    }
    if (rules.retargetBounces < 1 || rules.retargetBounces > mostCountedBounces) {
        return std::format("[Balls] RetargetAfterBounces must be 1 to {}", mostCountedBounces);
    }
    if (rules.launchHalfAngleDegrees <= 0.0f || rules.launchHalfAngleDegrees >= widestLaunchHalfAngle) {
        return std::format("[Balls] LaunchHalfAngleDegrees must be above 0 and below {}", widestLaunchHalfAngle);
    }
    if (rules.ticksPerSecond < fewestTicksPerSecond || rules.ticksPerSecond > mostTicksPerSecond) {
        return std::format("[Simulation] TicksPerSecond must be {} to {}", fewestTicksPerSecond, mostTicksPerSecond);
    }
    // A ball that moved further than its radius in one tick could pass through the corner of a brick.
    if (rules.ballSpeed / static_cast<float>(rules.ticksPerSecond) > rules.ballRadius) {
        return "[Balls] Speed must not move a ball more than its radius in one tick";
    }
    if (rules.maxTicksPerFrame < 1) {
        return "[Simulation] MaxTicksPerFrame must be at least 1";
    }
    if (rules.normalSpawnRate <= 0.0f) {
        return "[Normal] SpawnRate must be above 0";
    }
    return {};
}

}

Result<GameplayRules> parseGameplayRules(std::string_view text) {
    const SectionMap sections = splitSections(text);
    GameplayRules rules;
    for (const Result<bool>& read :
         {readRule(sections, {"Balls", "Radius"}, rules.ballRadius), readRule(sections, {"Balls", "Speed"}, rules.ballSpeed),
          readRule(sections, {"Balls", "RetargetAfterBounces"}, rules.retargetBounces),
          readRule(sections, {"Balls", "LaunchHalfAngleDegrees"}, rules.launchHalfAngleDegrees),
          readRule(sections, {"Simulation", "TicksPerSecond"}, rules.ticksPerSecond),
          readRule(sections, {"Simulation", "MaxTicksPerFrame"}, rules.maxTicksPerFrame),
          readRule(sections, {"Normal", "SpawnRate"}, rules.normalSpawnRate)}) {
        if (!read) {
            return Error{read.error()};
        }
    }
    if (const std::string problem = validate(rules); !problem.empty()) {
        return Error{problem};
    }
    return rules;
}
