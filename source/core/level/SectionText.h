#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// The text format the level files and Gameplay.txt share: "[Section]" headers, then lines of whitespace-separated
// tokens. Lines before the first header go to the section named "".
using SectionMap = std::map<std::string, std::vector<std::vector<std::string>>, std::less<>>;

SectionMap splitSections(std::string_view text);
