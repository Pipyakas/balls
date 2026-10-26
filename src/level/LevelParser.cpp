#include "level/LevelParser.h"

#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {

constexpr std::string_view boardSection = "Board";
constexpr std::string_view layoutSection = "Layout";
constexpr std::string_view brickTypesSection = "BrickTypes";
constexpr std::string_view emptySlotToken = "Empty";

using SectionMap = std::map<std::string, std::vector<std::vector<std::string>>, std::less<>>;

std::vector<std::string> splitWhitespace(std::string_view line) {
    std::vector<std::string> tokens;
    std::istringstream stream{std::string(line)};
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

SectionMap splitSections(std::string_view text) {
    SectionMap sections;
    std::string current;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const size_t lineEnd = std::min(text.find('\n', lineStart), text.size());
        const std::string_view line = trim(text.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            current = std::string(trim(line.substr(1, line.size() - 2)));
            sections[current];
            continue;
        }
        sections[current].push_back(splitWhitespace(line));
    }
    return sections;
}

std::expected<int, std::string> parseInt(std::string_view token) {
    int value = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size()) {
        return std::unexpected(std::format("expected an integer, got '{}'", token));
    }
    return value;
}

std::expected<std::map<std::string, int, std::less<>>, std::string> parseBoardSection(const SectionMap& sections) {
    const auto it = sections.find(boardSection);
    if (it == sections.end()) {
        return std::unexpected("missing [Board] section");
    }
    std::map<std::string, int, std::less<>> values;
    for (const auto& tokens : it->second) {
        if (tokens.size() != 2) {
            return std::unexpected("[Board] lines must be '<Key> <Value>'");
        }
        auto value = parseInt(tokens[1]);
        if (!value) {
            return std::unexpected(std::format("[Board] {}: {}", tokens[0], value.error()));
        }
        values[tokens[0]] = *value;
    }
    return values;
}

std::expected<int, std::string> requirePositive(const std::map<std::string, int, std::less<>>& values,
                                                std::string_view key) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return std::unexpected(std::format("[Board] missing '{}'", key));
    }
    if (it->second <= 0) {
        return std::unexpected(std::format("[Board] '{}' must be positive", key));
    }
    return it->second;
}

const std::vector<std::vector<std::string>>* findSection(const SectionMap& sections, std::string_view name) {
    const auto it = sections.find(name);
    return it == sections.end() ? nullptr : &it->second;
}

std::expected<std::optional<Launcher>, std::string> parseLauncherToken(std::string_view token) {
    if (token == emptySlotToken) {
        return std::optional<Launcher>{};
    }
    const auto open = token.find('(');
    if (open == std::string_view::npos || token.back() != ')') {
        return std::unexpected(std::format("launcher '{}' must look like Color(count)", token));
    }
    const auto color = parseBallColor(token.substr(0, open));
    if (!color) {
        return std::unexpected(std::format("unknown color in '{}'", token));
    }
    auto count = parseInt(token.substr(open + 1, token.size() - open - 2));
    if (!count || *count <= 0) {
        return std::unexpected(std::format("invalid ball count in '{}'", token));
    }
    return std::optional<Launcher>{Launcher{*color, *count}};
}

}

std::expected<BrickLayout, std::string> parseBrickLayout(std::string_view text) {
    const SectionMap sections = splitSections(text);
    auto board = parseBoardSection(sections);
    if (!board) {
        return std::unexpected(board.error());
    }
    BrickLayout layout;
    for (auto [key, target] : {std::pair{"Width", &layout.width}, std::pair{"Height", &layout.height},
                               std::pair{"PlayableHeight", &layout.playableHeight}}) {
        auto value = requirePositive(*board, key);
        if (!value) {
            return std::unexpected(value.error());
        }
        *target = *value;
    }
    if (layout.playableHeight < layout.height) {
        return std::unexpected("PlayableHeight must be at least Height");
    }

    const auto* rows = findSection(sections, layoutSection);
    if (!rows || static_cast<int>(rows->size()) != layout.height) {
        return std::unexpected(std::format("[Layout] must have {} rows", layout.height));
    }
    layout.cellIds.reserve(static_cast<size_t>(layout.width) * layout.height);
    for (int row = 0; row < layout.height; ++row) {
        const auto& tokens = (*rows)[row];
        if (static_cast<int>(tokens.size()) != layout.width) {
            return std::unexpected(std::format("[Layout] row {} must have {} cells", row, layout.width));
        }
        for (const auto& token : tokens) {
            if (token == emptySlotToken || token == ".") {
                layout.cellIds.push_back(emptyBrickId);
                continue;
            }
            auto id = parseInt(token);
            if (!id) {
                return std::unexpected(std::format("[Layout] row {}: {}", row, id.error()));
            }
            layout.cellIds.push_back(*id < 0 ? emptyBrickId : *id);
        }
    }

    const auto* typeLines = findSection(sections, brickTypesSection);
    if (!typeLines) {
        return std::unexpected("missing [BrickTypes] section");
    }
    for (const auto& tokens : *typeLines) {
        if (tokens.size() != 3) {
            return std::unexpected("[BrickTypes] lines must be '<Id> <Color> <Hits>'");
        }
        auto id = parseInt(tokens[0]);
        auto color = parseBallColor(tokens[1]);
        auto hits = parseInt(tokens[2]);
        if (!id || !color || !hits || *hits <= 0) {
            return std::unexpected(std::format("[BrickTypes] invalid line '{} {} {}'", tokens[0], tokens[1], tokens[2]));
        }
        if (layout.findType(*id)) {
            return std::unexpected(std::format("[BrickTypes] duplicate id {}", *id));
        }
        layout.types.push_back({*id, *color, *hits});
    }
    for (int& id : layout.cellIds) {
        if (id != emptyBrickId && !layout.findType(id)) {
            id = emptyBrickId;
        }
    }
    return layout;
}

std::expected<LauncherLayout, std::string> parseLauncherLayout(std::string_view text) {
    const SectionMap sections = splitSections(text);
    auto board = parseBoardSection(sections);
    if (!board) {
        return std::unexpected(board.error());
    }
    LauncherLayout layout;
    auto columns = requirePositive(*board, "Width");
    auto visibleRows = requirePositive(*board, "Height");
    if (!columns || !visibleRows) {
        return std::unexpected(columns ? visibleRows.error() : columns.error());
    }
    layout.columns = *columns;
    layout.visibleRows = *visibleRows;

    const auto* rows = findSection(sections, layoutSection);
    if (!rows) {
        return std::unexpected("missing [Layout] section");
    }
    for (size_t row = 0; row < rows->size(); ++row) {
        const auto& tokens = (*rows)[row];
        if (static_cast<int>(tokens.size()) != layout.columns) {
            return std::unexpected(std::format("[Layout] row {} must have {} launchers", row, layout.columns));
        }
        for (const auto& token : tokens) {
            auto launcher = parseLauncherToken(token);
            if (!launcher) {
                return std::unexpected(std::format("[Layout] row {}: {}", row, launcher.error()));
            }
            layout.slots.push_back(*launcher);
        }
    }
    return layout;
}

std::expected<std::string, std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("cannot open {}", path.string()));
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::expected<LevelData, std::string> loadLevel(const LevelFiles& files) {
    auto bricksText = readTextFile(files.bricksPath);
    if (!bricksText) {
        return std::unexpected(bricksText.error());
    }
    auto launchersText = readTextFile(files.launchersPath);
    if (!launchersText) {
        return std::unexpected(launchersText.error());
    }
    auto bricks = parseBrickLayout(*bricksText);
    if (!bricks) {
        return std::unexpected(std::format("{}: {}", files.bricksPath.filename().string(), bricks.error()));
    }
    auto launchers = parseLauncherLayout(*launchersText);
    if (!launchers) {
        return std::unexpected(std::format("{}: {}", files.launchersPath.filename().string(), launchers.error()));
    }
    return LevelData{files.name, std::move(*bricks), std::move(*launchers)};
}
