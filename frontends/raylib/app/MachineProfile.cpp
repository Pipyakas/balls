#include "app/MachineProfile.h"

#include "app/LaunchOptions.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace {

constexpr const char* profileFolder = "ColorBlastBalls";
constexpr const char* profileFileName = "machine.txt";

std::filesystem::path localAppDataDirectory() {
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, "LOCALAPPDATA") != 0 || !value) {
        return std::filesystem::temp_directory_path();
    }
    const std::filesystem::path directory(value);
    std::free(value);
    return directory;
}

}

std::filesystem::path MachineProfile::filePath() const {
    return localAppDataDirectory() / profileFolder / profileFileName;
}

void MachineProfile::load() {
    std::ifstream file(filePath());
    std::string backendName;
    BallCount limit = 0;
    while (file >> backendName >> limit) {
        if (const auto kind = parseBackendKind(backendName)) {
            ballLimits[*kind] = limit;
        }
    }
}

void MachineProfile::save() const {
    std::error_code error;
    std::filesystem::create_directories(filePath().parent_path(), error);
    std::ofstream file(filePath(), std::ios::trunc);
    for (const auto& [kind, limit] : ballLimits) {
        file << backendKindName(kind) << ' ' << limit << '\n';
    }
}

void MachineProfile::rememberBallLimit(BackendKind backend, BallCount limit) {
    ballLimits[backend] = limit;
    save();
}

std::optional<BallCount> MachineProfile::ballLimit(BackendKind backend) const {
    const auto it = ballLimits.find(backend);
    return it == ballLimits.end() ? std::nullopt : std::optional<BallCount>(it->second);
}
