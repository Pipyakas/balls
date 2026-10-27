#include "app/MachineProfile.h"

#include "app/LaunchOptions.h"
#include "app/PreferenceDirectory.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr const char* organizationName = "Pipyakas";
constexpr const char* applicationName = "ColorBlastBalls";
constexpr const char* profileFileName = "machine.txt";
constexpr const char* benchmarkFileName = "benchmark.txt";
constexpr const char* settingsFileName = "settings.txt";

} // namespace

std::filesystem::path settingsFilePath() {
    return preferenceDirectory() / settingsFileName;
}

std::filesystem::path preferenceDirectory() {
    char* directory = SDL_GetPrefPath(organizationName, applicationName);
    if (!directory) {
        return std::filesystem::temp_directory_path() / applicationName;
    }
    const std::filesystem::path path(reinterpret_cast<const char8_t*>(directory));
    SDL_free(directory);
    return path;
}

std::filesystem::path MachineProfile::filePath() const {
    return preferenceDirectory() / profileFileName;
}

std::filesystem::path MachineProfile::benchmarkFilePath() const {
    return preferenceDirectory() / benchmarkFileName;
}

void MachineProfile::load() {
    std::ifstream file(filePath());
    std::string backendName;
    double targetFps = 0.0;
    BallCount limit = 0;
    while (file >> backendName >> targetFps >> limit) {
        if (const auto kind = parseBackendKind(backendName)) {
            ballLimits[*kind] = {targetFps, limit};
        }
    }
    std::ifstream benchmarkFile(benchmarkFilePath());
    std::stringstream benchmarkText;
    benchmarkText << benchmarkFile.rdbuf();
    benchmarkRecord = parseBenchmarkRecord(benchmarkText.str());
}

void MachineProfile::save() const {
    std::error_code error;
    std::filesystem::create_directories(filePath().parent_path(), error);
    std::ofstream file(filePath(), std::ios::trunc);
    for (const auto& [kind, limit] : ballLimits) {
        file << backendKindName(kind) << ' ' << limit.targetFps << ' ' << limit.balls << '\n';
    }
}

void MachineProfile::rememberBallLimit(BackendKind backend, double targetFps, BallCount limit) {
    ballLimits[backend] = {targetFps, limit};
    save();
}

std::optional<MachineProfile::MeasuredLimit> MachineProfile::ballLimit(BackendKind backend) const {
    const auto it = ballLimits.find(backend);
    return it == ballLimits.end() ? std::nullopt : std::optional<MeasuredLimit>(it->second);
}

void MachineProfile::rememberBenchmark(BenchmarkRecord record) {
    std::error_code error;
    std::filesystem::create_directories(benchmarkFilePath().parent_path(), error);
    std::ofstream(benchmarkFilePath(), std::ios::trunc) << serializeBenchmarkRecord(record);
    benchmarkRecord = std::move(record);
}
