#include "level/SectionText.h"

#include <algorithm>
#include <sstream>

namespace {

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
