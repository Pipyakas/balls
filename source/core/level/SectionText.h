#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// The text format the level files and Gameplay.txt share: "[Section]" headers, then lines of whitespace-separated
// tokens. Lines before the first header go to the section named "". Lines are views into the text, which must
// outlive the map: an 8192x8192 level has 67M brick lines, and a string per token took 85 s and gigabytes to build.
// Section names match in any case, and a UTF-8 byte order mark, "#" lines and "//" lines are skipped: hand-made and
// generated maps carry them, and none of them can be a layout row.
struct CaseInsensitiveLess {
    using is_transparent = void;
    bool operator()(std::string_view left, std::string_view right) const;
};
using SectionMap = std::map<std::string, std::vector<std::string_view>, CaseInsensitiveLess>;

SectionMap splitSections(std::string_view text);
std::vector<std::string_view> splitTokens(std::string_view line);
bool equalsIgnoringCase(std::string_view left, std::string_view right);

// Calls visit(token) for each token of the line without allocating; stops early when visit returns false.
template <typename Visit>
void forEachToken(std::string_view line, Visit&& visit) {
    size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t' || line[position] == '\r')) {
            ++position;
        }
        if (position == line.size()) {
            return;
        }
        const size_t start = position;
        while (position < line.size() && line[position] != ' ' && line[position] != '\t' && line[position] != '\r') {
            ++position;
        }
        if (!visit(line.substr(start, position - start))) {
            return;
        }
    }
}
