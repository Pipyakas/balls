#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct CaseInsensitiveLess {
    using is_transparent = void;
    bool operator()(std::string_view left, std::string_view right) const;
};
using SectionMap = std::map<std::string, std::vector<std::string_view>, CaseInsensitiveLess>;

SectionMap splitSections(std::string_view text);
std::vector<std::string_view> splitTokens(std::string_view line);
bool equalsIgnoringCase(std::string_view left, std::string_view right);

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
