#pragma once

#include <cstdlib>
#include <optional>
#include <string>

// The value of an environment variable, or nothing when it is unset. MSVC deprecates getenv as unsafe, so Windows
// reads it with _dupenv_s.
inline std::optional<std::string> environmentVariable(const char* name) {
#if defined(_MSC_VER)
    char* buffer = nullptr;
    size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}
