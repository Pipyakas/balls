#include "level/LevelParser.h"

#include "level/GameplayRules.h"
#include "level/SectionText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view boardSection = "Board";
constexpr std::string_view layoutSection = "Layout";
constexpr std::string_view brickTypesSection = "BrickTypes";
constexpr std::string_view emptySlotToken = "Empty";
// Brick ids up to this many times the number of types, plus the floor, are checked with a flag per id.
constexpr size_t denseIdSlack = 4;
constexpr size_t denseIdFloor = 1 << 16;
// The simulation indexes the target field (one entry per padded cell and color) and every padded cell with 32-bit
// integers, and the GPU packs a brick's index into 28 bits of a cell word: boards past these would overflow them.
constexpr uint64_t mostTargetFieldEntries = 0x7FFFFFFF;
constexpr size_t mostBricks = size_t{1} << 28;

using BoardValues = std::map<std::string, int, std::less<>>;

Result<int> parseInt(std::string_view token) {
    int value = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size()) {
        return Error{std::format("expected an integer, got '{}'", token)};
    }
    return value;
}

Result<BoardValues> parseBoardSection(const SectionMap& sections) {
    const auto it = sections.find(boardSection);
    if (it == sections.end()) {
        return Error{"missing [Board] section"};
    }
    BoardValues values;
    for (const std::string_view line : it->second) {
        const std::vector<std::string_view> tokens = splitTokens(line);
        if (tokens.size() != 2) {
            return Error{"[Board] lines must be '<Key> <Value>'"};
        }
        auto value = parseInt(tokens[1]);
        if (!value) {
            return Error{std::format("[Board] {}: {}", tokens[0], value.error())};
        }
        values[std::string(tokens[0])] = *value;
    }
    return values;
}

Result<int> requirePositive(const BoardValues& values,
                                                std::string_view key) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return Error{std::format("[Board] missing '{}'", key)};
    }
    if (it->second <= 0) {
        return Error{std::format("[Board] '{}' must be positive", key)};
    }
    return it->second;
}

const std::vector<std::string_view>* findSection(const SectionMap& sections, std::string_view name) {
    const auto it = sections.find(name);
    return it == sections.end() ? nullptr : &it->second;
}

Result<std::optional<Launcher>> parseLauncherToken(std::string_view token) {
    if (token == emptySlotToken) {
        return std::optional<Launcher>{};
    }
    const auto open = token.find('(');
    if (open == std::string_view::npos || token.back() != ')') {
        return Error{std::format("launcher '{}' must look like Color(count)", token)};
    }
    const auto color = parseBallColor(token.substr(0, open));
    if (!color) {
        return Error{std::format("unknown color in '{}'", token)};
    }
    auto count = parseInt(token.substr(open + 1, token.size() - open - 2));
    if (!count || *count <= 0) {
        return Error{std::format("invalid ball count in '{}'", token)};
    }
    return std::optional<Launcher>{Launcher{*color, *count}};
}

}

Result<BrickLayout> parseBrickLayout(std::string_view text) {
    const SectionMap sections = splitSections(text);
    auto board = parseBoardSection(sections);
    if (!board) {
        return Error{board.error()};
    }
    BrickLayout layout;
    for (auto [key, target] : {std::pair{"Width", &layout.width}, std::pair{"Height", &layout.height},
                               std::pair{"PlayableHeight", &layout.playableHeight}}) {
        auto value = requirePositive(*board, key);
        if (!value) {
            return Error{value.error()};
        }
        *target = *value;
    }
    if (layout.playableHeight < layout.height) {
        return Error{"PlayableHeight must be at least Height"};
    }
    const uint64_t paddedCells = (static_cast<uint64_t>(layout.width) + 2) * (static_cast<uint64_t>(layout.playableHeight) + 2);
    if (paddedCells * ballColorCount > mostTargetFieldEntries) {
        return Error{std::format("the board is too large: {}x{} cells, at most {} cells including the open rows",
                                 layout.width, layout.playableHeight, mostTargetFieldEntries / ballColorCount)};
    }

    const auto* rows = findSection(sections, layoutSection);
    if (!rows || static_cast<int>(rows->size()) != layout.height) {
        return Error{std::format("[Layout] must have {} rows", layout.height)};
    }
    layout.cellIds.reserve(static_cast<size_t>(layout.width) * layout.height);
    for (int row = 0; row < layout.height; ++row) {
        const size_t rowStart = layout.cellIds.size();
        std::string badToken;
        forEachToken((*rows)[row], [&](std::string_view token) {
            if (token == emptySlotToken || token == ".") {
                layout.cellIds.push_back(emptyBrickId);
                return true;
            }
            auto id = parseInt(token);
            if (!id) {
                badToken = id.error();
                return false;
            }
            layout.cellIds.push_back(*id < 0 ? emptyBrickId : *id);
            return true;
        });
        if (!badToken.empty()) {
            return Error{std::format("[Layout] row {}: {}", row, badToken)};
        }
        if (layout.cellIds.size() - rowStart != static_cast<size_t>(layout.width)) {
            return Error{std::format("[Layout] row {} must have {} cells", row, layout.width)};
        }
    }

    const auto* typeLines = findSection(sections, brickTypesSection);
    if (!typeLines) {
        return Error{"missing [BrickTypes] section"};
    }
    if (typeLines->size() > mostBricks) {
        return Error{std::format("too many bricks: {}, at most {}", typeLines->size(), mostBricks)};
    }
    layout.types.reserve(typeLines->size());
    for (const std::string_view line : *typeLines) {
        std::array<std::string_view, 3> tokens;
        size_t count = 0;
        forEachToken(line, [&](std::string_view token) {
            if (count < tokens.size()) {
                tokens[count] = token;
            }
            return ++count <= tokens.size();
        });
        if (count != tokens.size()) {
            return Error{"[BrickTypes] lines must be '<Id> <Color> <Hits>'"};
        }
        auto id = parseInt(tokens[0]);
        auto color = parseBallColor(tokens[1]);
        auto hits = parseInt(tokens[2]);
        if (!id || !color || !hits || *hits <= 0) {
            return Error{std::format("[BrickTypes] invalid line '{} {} {}'", tokens[0], tokens[1], tokens[2])};
        }
        layout.types.push_back({*id, *color, *hits});
    }
    // Ids are almost always 0..N-1, so a flag per id finds duplicates and unknown ids without hashing 67M of them.
    int largestId = -1;
    bool negativeId = false;
    for (const BrickType& type : layout.types) {
        largestId = std::max(largestId, type.id);
        negativeId = negativeId || type.id < 0;
    }
    const bool dense = !negativeId && static_cast<size_t>(largestId) < layout.types.size() * denseIdSlack + denseIdFloor;
    if (dense) {
        std::vector<uint8_t> known(static_cast<size_t>(largestId) + 1, 0);
        for (const BrickType& type : layout.types) {
            if (std::exchange(known[static_cast<size_t>(type.id)], 1) != 0) {
                return Error{std::format("[BrickTypes] duplicate id {}", type.id)};
            }
        }
        for (int& id : layout.cellIds) {
            if (id != emptyBrickId && (id > largestId || !known[static_cast<size_t>(id)])) {
                id = emptyBrickId;
            }
        }
        return layout;
    }
    std::unordered_set<int> typeIds;
    typeIds.reserve(layout.types.size());
    for (const BrickType& type : layout.types) {
        if (!typeIds.insert(type.id).second) {
            return Error{std::format("[BrickTypes] duplicate id {}", type.id)};
        }
    }
    for (int& id : layout.cellIds) {
        if (id != emptyBrickId && !typeIds.contains(id)) {
            id = emptyBrickId;
        }
    }
    return layout;
}

Result<LauncherLayout> parseLauncherLayout(std::string_view text) {
    const SectionMap sections = splitSections(text);
    auto board = parseBoardSection(sections);
    if (!board) {
        return Error{board.error()};
    }
    LauncherLayout layout;
    auto columns = requirePositive(*board, "Width");
    auto visibleRows = requirePositive(*board, "Height");
    if (!columns || !visibleRows) {
        return Error{columns ? visibleRows.error() : columns.error()};
    }
    layout.columns = *columns;
    layout.visibleRows = *visibleRows;

    const auto* rows = findSection(sections, layoutSection);
    if (!rows) {
        return Error{"missing [Layout] section"};
    }
    for (size_t row = 0; row < rows->size(); ++row) {
        const std::vector<std::string_view> tokens = splitTokens((*rows)[row]);
        if (static_cast<int>(tokens.size()) != layout.columns) {
            return Error{std::format("[Layout] row {} must have {} launchers", row, layout.columns)};
        }
        for (const auto& token : tokens) {
            auto launcher = parseLauncherToken(token);
            if (!launcher) {
                return Error{std::format("[Layout] row {}: {}", row, launcher.error())};
            }
            layout.slots.push_back(*launcher);
        }
    }
    return layout;
}

Result<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error{std::format("cannot open {}", path.string())};
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(std::max<std::streamoff>(size, 0)), '\0');
    if (!content.empty() && !file.read(content.data(), static_cast<std::streamsize>(content.size()))) {
        return Error{std::format("cannot read {}", path.string())};
    }
    return content;
}

Result<LevelData> loadLevel(const LevelFiles& files) {
    auto bricksText = readTextFile(files.bricksPath);
    if (!bricksText) {
        return Error{bricksText.error()};
    }
    auto launchersText = readTextFile(files.launchersPath);
    if (!launchersText) {
        return Error{launchersText.error()};
    }
    auto bricks = parseBrickLayout(*bricksText);
    if (!bricks) {
        return Error{std::format("{}: {}", files.bricksPath.filename().string(), bricks.error())};
    }
    auto launchers = parseLauncherLayout(*launchersText);
    if (!launchers) {
        return Error{std::format("{}: {}", files.launchersPath.filename().string(), launchers.error())};
    }
    // Gameplay.txt sits in the levels folder, the parent of the normal/ and performance/ folders; a level kept
    // anywhere else plays by the built-in rules.
    GameplayRules rules;
    const std::filesystem::path rulesPath = files.bricksPath.parent_path().parent_path() / gameplayRulesFileName;
    if (std::filesystem::exists(rulesPath)) {
        auto rulesText = readTextFile(rulesPath);
        if (!rulesText) {
            return Error{rulesText.error()};
        }
        auto parsedRules = parseGameplayRules(*rulesText);
        if (!parsedRules) {
            return Error{std::format("{}: {}", gameplayRulesFileName, parsedRules.error())};
        }
        rules = *parsedRules;
    }
    return LevelData{files.name, std::move(*bricks), std::move(*launchers), rules};
}
