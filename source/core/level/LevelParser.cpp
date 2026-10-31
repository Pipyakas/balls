#include "level/LevelParser.h"

#include "level/GameplayRules.h"
#include "level/SectionText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <limits>
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
// A board that leaves PlayableHeight out gets this much open space below its bricks: a quarter of the brick rows, and
// at least the sample DA_49Bricks gap.
constexpr int minimumDefaultOpenRows = 7;
constexpr int defaultOpenRowsDivisor = 4;

using BoardValues = std::map<std::string, int, CaseInsensitiveLess>;

// Cell tokens that mean "no brick" besides the format's -1: the sample Empty, and what hand-drawn maps use.
constexpr std::array<std::string_view, 5> emptyCellTokens = {"Empty", ".", "-", "_", "x"};
// A launcher file without Height shows this many rows, the sample Normal levels' count.
constexpr int defaultVisibleLauncherRows = 3;
// The loading screen's row count moves on every this many layout rows: often enough to watch, rarely enough to cost
// nothing beside the parse.
constexpr int progressRowStride = 64;
constexpr double bytesPerKilobyte = 1024.0;
constexpr double bytesPerMegabyte = bytesPerKilobyte * 1024.0;

std::string fileSizeText(size_t bytes) {
    const double size = static_cast<double>(bytes);
    return size < bytesPerMegabyte ? std::format("{:.1f} KB", size / bytesPerKilobyte)
                                   : std::format("{:.1f} MB", size / bytesPerMegabyte);
}

void reportLayoutRow(LoadProgress* progress, int row, int rows) {
    if (progress && row % progressRowStride == 0) {
        progress->setDetail(std::format("row {} of {}", groupedCount(static_cast<uint64_t>(row)),
                                        groupedCount(static_cast<uint64_t>(rows))));
    }
}

Result<int> parseInt(std::string_view token) {
    int value = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size()) {
        return Error{std::format("expected an integer, got '{}'", token)};
    }
    return value;
}

// "Width 7", and the "Width: 7", "Width=7" and "Width = 7" that hand-written maps use.
std::optional<std::pair<std::string_view, std::string_view>> splitBoardLine(std::string_view line) {
    const size_t separator = line.find_first_of(":=");
    if (separator != std::string_view::npos) {
        const std::vector<std::string_view> key = splitTokens(line.substr(0, separator));
        const std::vector<std::string_view> value = splitTokens(line.substr(separator + 1));
        if (key.size() == 1 && value.size() == 1) {
            return std::pair{key[0], value[0]};
        }
        return std::nullopt;
    }
    const std::vector<std::string_view> tokens = splitTokens(line);
    if (tokens.size() != 2) {
        return std::nullopt;
    }
    return std::pair{tokens[0], tokens[1]};
}

// Lines that are not a key and a number are skipped: a key the parser needs and cannot read is reported as missing.
Result<BoardValues> parseBoardSection(const SectionMap& sections) {
    const auto it = sections.find(boardSection);
    if (it == sections.end()) {
        return Error{"missing [Board] section"};
    }
    BoardValues values;
    for (const std::string_view line : it->second) {
        const auto keyValue = splitBoardLine(line);
        if (!keyValue) {
            continue;
        }
        if (auto value = parseInt(keyValue->second)) {
            values[std::string(keyValue->first)] = *value;
        }
    }
    return values;
}

std::optional<int> positiveValue(const BoardValues& values, std::string_view key) {
    const auto it = values.find(key);
    return it == values.end() || it->second <= 0 ? std::nullopt : std::optional<int>{it->second};
}

Result<int> requirePositive(const BoardValues& values, std::string_view key) {
    if (const std::optional<int> value = positiveValue(values, key)) {
        return *value;
    }
    return Error{values.contains(key) ? std::format("[Board] '{}' must be positive", key)
                                      : std::format("[Board] missing '{}'", key)};
}

const std::vector<std::string_view>* findSection(const SectionMap& sections, std::string_view name) {
    const auto it = sections.find(name);
    return it == sections.end() ? nullptr : &it->second;
}

int defaultPlayableHeight(int height) {
    return height + std::max(minimumDefaultOpenRows, height / defaultOpenRowsDivisor);
}

std::string checkBoardSize(int width, int playableHeight) {
    const uint64_t paddedCells = (static_cast<uint64_t>(width) + 2) * (static_cast<uint64_t>(playableHeight) + 2);
    if (paddedCells * ballColorCount > mostTargetFieldEntries) {
        return std::format("the board is too large: {}x{} cells, at most {} cells including the open rows", width,
                           playableHeight, mostTargetFieldEntries / ballColorCount);
    }
    return {};
}

Result<int> parseCellToken(std::string_view token) {
    int id = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), id);
    if (error == std::errc{} && end == token.data() + token.size()) {
        return id < 0 ? emptyBrickId : id;
    }
    for (const std::string_view empty : emptyCellTokens) {
        if (equalsIgnoringCase(token, empty)) {
            return emptyBrickId;
        }
    }
    return Error{std::format("expected a brick id, got '{}'", token)};
}

// The format's fast path: exactly Height rows of exactly Width cells. False when the rows disagree with the header.
Result<bool> readExactLayout(const std::vector<std::string_view>& rows, BrickLayout& layout, LoadProgress* progress) {
    if (static_cast<int>(rows.size()) != layout.height) {
        return false;
    }
    layout.cellIds.reserve(static_cast<size_t>(layout.width) * layout.height);
    for (int row = 0; row < layout.height; ++row) {
        reportLayoutRow(progress, row, layout.height);
        const size_t rowStart = layout.cellIds.size();
        std::string badToken;
        forEachToken(rows[row], [&](std::string_view token) {
            auto id = parseCellToken(token);
            if (!id) {
                badToken = id.error();
                return false;
            }
            layout.cellIds.push_back(*id);
            return layout.cellIds.size() - rowStart <= static_cast<size_t>(layout.width);
        });
        if (!badToken.empty()) {
            return Error{std::format("[Layout] row {}: {}", row, badToken)};
        }
        if (layout.cellIds.size() - rowStart != static_cast<size_t>(layout.width)) {
            layout.cellIds.clear();
            return false;
        }
    }
    return true;
}

// Rows that disagree with the header: the board takes the rows as written, as wide as the longest, and pads the
// shorter ones with empty cells, so no brick the file places is lost. Returns whether any row was padded.
Result<bool> readRaggedLayout(const std::vector<std::string_view>& rows, BrickLayout& layout, LoadProgress* progress) {
    int widest = 0;
    int narrowest = std::numeric_limits<int>::max();
    for (const std::string_view row : rows) {
        int cells = 0;
        forEachToken(row, [&](std::string_view) {
            ++cells;
            return true;
        });
        widest = std::max(widest, cells);
        narrowest = std::min(narrowest, cells);
    }
    if (rows.empty() || widest == 0) {
        return Error{"[Layout] has no rows"};
    }
    layout.width = std::max(layout.width, widest);
    layout.height = static_cast<int>(rows.size());
    if (const std::string tooLarge = checkBoardSize(layout.width, layout.height); !tooLarge.empty()) {
        return Error{tooLarge};
    }
    layout.cellIds.assign(static_cast<size_t>(layout.width) * layout.height, emptyBrickId);
    for (int row = 0; row < layout.height; ++row) {
        reportLayoutRow(progress, row, layout.height);
        size_t cell = static_cast<size_t>(row) * layout.width;
        std::string badToken;
        forEachToken(rows[row], [&](std::string_view token) {
            auto id = parseCellToken(token);
            if (!id) {
                badToken = id.error();
                return false;
            }
            layout.cellIds[cell++] = *id;
            return true;
        });
        if (!badToken.empty()) {
            return Error{std::format("[Layout] row {}: {}", row, badToken)};
        }
    }
    return narrowest < layout.width;
}

// "<Id> <Color> <Hits>", with anything after the hits ignored; a line that cannot be read is skipped, and its cells
// stay empty like any id without a type.
std::optional<BrickType> parseBrickType(std::string_view line) {
    std::array<std::string_view, 3> tokens;
    size_t count = 0;
    forEachToken(line, [&](std::string_view token) {
        tokens[count++] = token;
        return count < tokens.size();
    });
    if (count != tokens.size()) {
        return std::nullopt;
    }
    auto id = parseInt(tokens[0]);
    auto color = parseBallColor(tokens[1]);
    auto hits = parseInt(tokens[2]);
    if (!id || !color || !hits || *hits <= 0) {
        return std::nullopt;
    }
    return BrickType{*id, *color, *hits};
}

void note(MapRepairs* repairs, std::string text) {
    if (repairs) {
        repairs->push_back(std::move(text));
    }
}

const char* plural(size_t count) { return count == 1 ? "" : "s"; }

// Keeps the first type of each id, and clears cells whose id has no type. Returns how many repeated types it dropped.
size_t dropDuplicateTypesAndUnknownCells(BrickLayout& layout) {
    const size_t typeCount = layout.types.size();
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
        std::erase_if(layout.types, [&](const BrickType& type) {
            return std::exchange(known[static_cast<size_t>(type.id)], uint8_t{1}) != 0;
        });
        for (int& id : layout.cellIds) {
            if (id != emptyBrickId && (id > largestId || !known[static_cast<size_t>(id)])) {
                id = emptyBrickId;
            }
        }
        return typeCount - layout.types.size();
    }
    std::unordered_set<int> typeIds;
    typeIds.reserve(layout.types.size());
    std::erase_if(layout.types, [&](const BrickType& type) { return !typeIds.insert(type.id).second; });
    for (int& id : layout.cellIds) {
        if (id != emptyBrickId && !typeIds.contains(id)) {
            id = emptyBrickId;
        }
    }
    return typeCount - layout.types.size();
}

// "Red(10)", and "Red (10)" split by a space, which arrives as two tokens.
std::vector<std::string> launcherTokens(std::string_view row) {
    std::vector<std::string> tokens;
    forEachToken(row, [&](std::string_view token) {
        if (token.front() == '(' && !tokens.empty() && tokens.back().find('(') == std::string::npos) {
            tokens.back() += token;
        } else {
            tokens.emplace_back(token);
        }
        return true;
    });
    return tokens;
}

// A launcher of no balls is an empty slot.
Result<std::optional<Launcher>> parseLauncherToken(std::string_view token) {
    if (equalsIgnoringCase(token, emptySlotToken)) {
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
    if (!count) {
        return Error{std::format("invalid ball count in '{}'", token)};
    }
    if (*count <= 0) {
        return std::optional<Launcher>{};
    }
    return std::optional<Launcher>{Launcher{*color, *count}};
}

}

Result<BrickLayout> parseBrickLayout(std::string_view text, MapRepairs* repairs, LoadProgress* progress) {
    beginLoadStep(progress, "Finding the sections");
    const SectionMap sections = splitSections(text);
    auto board = parseBoardSection(sections);
    if (!board) {
        return Error{board.error()};
    }
    const auto* rows = findSection(sections, layoutSection);
    if (!rows) {
        return Error{"missing [Layout] section"};
    }
    BrickLayout layout;
    auto width = requirePositive(*board, "Width");
    if (!width) {
        return Error{width.error()};
    }
    layout.width = *width;
    const std::optional<int> declaredHeight = positiveValue(*board, "Height");
    layout.height = declaredHeight.value_or(static_cast<int>(rows->size()));
    const int declaredWidth = layout.width;
    const std::optional<int> playableHeight = positiveValue(*board, "PlayableHeight");
    if (const std::string tooLarge = checkBoardSize(layout.width, std::max(layout.height, playableHeight.value_or(0)));
        !tooLarge.empty()) {
        return Error{tooLarge};
    }

    beginLoadStep(progress, std::format("Reading the {} x {} brick layout", groupedCount(static_cast<uint64_t>(layout.width)),
                                        groupedCount(static_cast<uint64_t>(layout.height))));
    auto exact = readExactLayout(*rows, layout, progress);
    if (!exact) {
        return Error{exact.error()};
    }
    if (!*exact) {
        setLoadDetail(progress, "rows disagree with the header; reading them as written");
        auto padded = readRaggedLayout(*rows, layout, progress);
        if (!padded) {
            return Error{padded.error()};
        }
        if (layout.width != declaredWidth) {
            note(repairs, std::format("layout rows up to {} cells wide, not Width {}", layout.width, declaredWidth));
        }
        if (declaredHeight && layout.height != *declaredHeight) {
            note(repairs, std::format("{} layout row{}, not Height {}", layout.height,
                                      plural(static_cast<size_t>(layout.height)), *declaredHeight));
        }
        if (*padded) {
            note(repairs, std::format("short layout rows padded to {} cells", layout.width));
        }
    }
    setLoadDetail(progress, std::format("{} rows", groupedCount(static_cast<uint64_t>(layout.height))));
    if (!declaredHeight) {
        note(repairs, std::format("no Height; used the {} layout rows", layout.height));
    }
    // A PlayableHeight left out, or shorter than the brick rows, leaves the bricks no room to fall into.
    if (playableHeight && *playableHeight >= layout.height) {
        layout.playableHeight = *playableHeight;
    } else {
        layout.playableHeight = defaultPlayableHeight(layout.height);
        note(repairs, playableHeight ? std::format("PlayableHeight {} is below the {} brick rows; used {}", *playableHeight,
                                                   layout.height, layout.playableHeight)
                                     : std::format("no PlayableHeight; used {}", layout.playableHeight));
    }
    if (const std::string tooLarge = checkBoardSize(layout.width, layout.playableHeight); !tooLarge.empty()) {
        return Error{tooLarge};
    }

    if (const auto* typeLines = findSection(sections, brickTypesSection)) {
        beginLoadStep(progress, std::format("Reading {} brick types", groupedCount(typeLines->size())));
        if (typeLines->size() > mostBricks) {
            return Error{std::format("too many bricks: {}, at most {}", typeLines->size(), mostBricks)};
        }
        layout.types.reserve(typeLines->size());
        for (const std::string_view line : *typeLines) {
            if (const std::optional<BrickType> type = parseBrickType(line)) {
                layout.types.push_back(*type);
            }
        }
        if (const size_t skipped = typeLines->size() - layout.types.size(); skipped > 0) {
            note(repairs, std::format("{} unreadable [BrickTypes] line{} skipped", skipped, plural(skipped)));
        }
    } else {
        note(repairs, "no [BrickTypes]; the board has no bricks");
    }
    beginLoadStep(progress, "Matching cells to brick types");
    if (const size_t repeated = dropDuplicateTypesAndUnknownCells(layout); repeated > 0) {
        note(repairs, std::format("{} repeated brick id{} skipped", repeated, plural(repeated)));
    }
    return layout;
}

Result<LauncherLayout> parseLauncherLayout(std::string_view text, MapRepairs* repairs) {
    const SectionMap sections = splitSections(text);
    // Both [Board] values have defaults from the rows, so a launcher file may leave the whole section out.
    auto board = sections.contains(boardSection) ? parseBoardSection(sections) : Result<BoardValues>(BoardValues{});
    if (!board) {
        return Error{board.error()};
    }
    const auto* rows = findSection(sections, layoutSection);
    if (!rows) {
        return Error{"missing [Layout] section"};
    }
    std::vector<std::vector<std::optional<Launcher>>> parsedRows;
    parsedRows.reserve(rows->size());
    size_t widest = 0;
    size_t narrowest = std::numeric_limits<size_t>::max();
    size_t emptiedLaunchers = 0;
    for (size_t row = 0; row < rows->size(); ++row) {
        std::vector<std::optional<Launcher>>& parsed = parsedRows.emplace_back();
        for (const std::string& token : launcherTokens((*rows)[row])) {
            auto launcher = parseLauncherToken(token);
            if (!launcher) {
                return Error{std::format("[Layout] row {}: {}", row, launcher.error())};
            }
            if (!launcher->has_value() && !equalsIgnoringCase(token, emptySlotToken)) {
                ++emptiedLaunchers;
            }
            parsed.push_back(*launcher);
        }
        widest = std::max(widest, parsed.size());
        narrowest = std::min(narrowest, parsed.size());
    }
    if (widest == 0) {
        return Error{"[Layout] has no launchers"};
    }
    // Rows wider than Width widen the launcher; shorter ones are padded with empty slots.
    LauncherLayout layout;
    const std::optional<int> declaredColumns = positiveValue(*board, "Width");
    const std::optional<int> declaredRows = positiveValue(*board, "Height");
    layout.columns = std::max(declaredColumns.value_or(0), static_cast<int>(widest));
    layout.visibleRows = declaredRows.value_or(std::min(static_cast<int>(parsedRows.size()), defaultVisibleLauncherRows));
    if (!declaredColumns) {
        note(repairs, std::format("no Width; used {} launcher columns", layout.columns));
    } else if (narrowest != widest || static_cast<int>(widest) != *declaredColumns) {
        note(repairs, std::format("launcher rows padded to {} columns", layout.columns));
    }
    if (!declaredRows) {
        note(repairs, std::format("no Height; showing {} launcher rows", layout.visibleRows));
    }
    if (emptiedLaunchers > 0) {
        note(repairs, std::format("{} launcher{} of 0 balls left empty", emptiedLaunchers, plural(emptiedLaunchers)));
    }
    layout.slots.reserve(parsedRows.size() * layout.columns);
    for (std::vector<std::optional<Launcher>>& parsed : parsedRows) {
        parsed.resize(static_cast<size_t>(layout.columns));
        layout.slots.insert(layout.slots.end(), parsed.begin(), parsed.end());
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

Result<LevelData> loadLevel(const LevelFiles& files, LoadProgress* progress) {
    auto readReported = [progress](const std::filesystem::path& path) {
        beginLoadStep(progress, std::format("Reading {}", path.filename().string()));
        auto text = readTextFile(path);
        if (text) {
            setLoadDetail(progress, fileSizeText(text->size()));
        }
        return text;
    };
    auto bricksText = readReported(files.bricksPath);
    if (!bricksText) {
        return Error{bricksText.error()};
    }
    auto launchersText = readReported(files.launchersPath);
    if (!launchersText) {
        return Error{launchersText.error()};
    }
    MapRepairs bricksRepairs;
    MapRepairs launchersRepairs;
    auto bricks = parseBrickLayout(*bricksText, &bricksRepairs, progress);
    if (!bricks) {
        return Error{std::format("{}: {}", files.bricksPath.filename().string(), bricks.error())};
    }
    beginLoadStep(progress, "Reading the launchers");
    auto launchers = parseLauncherLayout(*launchersText, &launchersRepairs);
    if (!launchers) {
        return Error{std::format("{}: {}", files.launchersPath.filename().string(), launchers.error())};
    }
    setLoadDetail(progress, std::format("{} column{}, {} row{}", launchers->columns,
                                        plural(static_cast<size_t>(launchers->columns)), launchers->rowCount(),
                                        plural(static_cast<size_t>(launchers->rowCount()))));
    // Gameplay.txt sits in the levels folder, the parent of the normal/ and performance/ folders; a level kept
    // anywhere else plays by the built-in rules.
    GameplayRules rules;
    const std::filesystem::path rulesPath = files.bricksPath.parent_path().parent_path() / gameplayRulesFileName;
    if (std::filesystem::exists(rulesPath)) {
        beginLoadStep(progress, std::format("Reading {}", gameplayRulesFileName));
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
    LevelData level{files.name, std::move(*bricks), std::move(*launchers), rules, {}};
    for (const auto& [path, repairs] :
         {std::pair{&files.bricksPath, &bricksRepairs}, std::pair{&files.launchersPath, &launchersRepairs}}) {
        for (const std::string& repair : *repairs) {
            level.repairs.push_back(std::format("{}: {}", path->filename().string(), repair));
        }
    }
    return level;
}

std::string levelRepairNotice(const LevelData& level) {
    if (level.repairs.empty()) {
        return {};
    }
    std::string notice = std::format("{} does not follow the map format and was repaired: {}", level.name,
                                     level.repairs.front());
    if (level.repairs.size() > 1) {
        notice += std::format(" (and {} more)", level.repairs.size() - 1);
    }
    return notice;
}
