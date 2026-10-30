#include "level/SectionText.h"

#include <algorithm>

namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}

std::vector<std::string_view> splitTokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    forEachToken(line, [&](std::string_view token) {
        tokens.push_back(token);
        return true;
    });
    return tokens;
}

SectionMap splitSections(std::string_view text) {
    SectionMap sections;
    std::vector<std::string_view>* current = &sections[""];
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const size_t lineEnd = std::min(text.find('\n', lineStart), text.size());
        const std::string_view line = trim(text.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            current = &sections[std::string(trim(line.substr(1, line.size() - 2)))];
            continue;
        }
        current->push_back(line);
    }
    return sections;
}
