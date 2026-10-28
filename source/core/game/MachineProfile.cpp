#include "game/MachineProfile.h"

#include "sim/BackendSelection.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

constexpr const char* limitsFileName = "machine.txt";
constexpr const char* benchmarkFileName = "benchmark.txt";
// Unity's own profile file before the core owned it; only its limit lines are carried over.
constexpr const char* legacyUnityFileName = "machine-profile.txt";
constexpr std::string_view legacyLimitTag = "limit";

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::vector<std::string_view> splitFields(std::string_view line, char separator) {
    std::vector<std::string_view> fields;
    while (!line.empty()) {
        const size_t end = line.find(separator);
        if (const std::string_view field = line.substr(0, end); !field.empty()) {
            fields.push_back(field);
        }
        line = end == std::string_view::npos ? std::string_view() : line.substr(end + 1);
    }
    return fields;
}

template <typename T>
std::optional<T> parseNumber(std::string_view text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

void addLimit(MeasuredBallLimits& limits, std::optional<BackendKind> kind, std::string_view fps, std::string_view balls) {
    const std::optional<double> targetFps = parseNumber<double>(fps);
    const std::optional<BallCount> count = parseNumber<BallCount>(balls);
    if (kind && targetFps && count) {
        limits[*kind] = {*targetFps, *count};
    }
}

std::optional<BackendKind> storedKind(std::string_view text) {
    const std::optional<StoredBackend> stored = parseStoredBackend(text);
    return stored ? std::optional<BackendKind>(stored->kind) : std::nullopt;
}

} // namespace

std::string serializeBallLimits(const MeasuredBallLimits& limits) {
    std::ostringstream text;
    for (const auto& [kind, limit] : limits) {
        text << storedBackendName(kind) << ' ' << limit.targetFps << ' ' << limit.balls << '\n';
    }
    return text.str();
}

MeasuredBallLimits parseBallLimits(std::string_view text) {
    MeasuredBallLimits limits;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.find('\t') != std::string_view::npos) {
            const std::vector<std::string_view> fields = splitFields(line, '\t');
            if (fields.size() >= 4 && fields[0] == legacyLimitTag) {
                // Unity wrote display names, where "Hybrid" is the composer and not the legacy CUDA+AVX2 pair.
                addLimit(limits, backendKindFromName(fields[1]), fields[3], fields[2]);
            }
            continue;
        }
        const std::vector<std::string_view> fields = splitFields(line, ' ');
        if (fields.size() == 3) {
            addLimit(limits, storedKind(fields[0]), fields[1], fields[2]);
        }
    }
    return limits;
}

void MachineProfile::load() {
    std::error_code error;
    const std::filesystem::path limitsPath = folder / limitsFileName;
    const std::filesystem::path legacyPath = folder / legacyUnityFileName;
    if (std::filesystem::exists(limitsPath, error)) {
        ballLimits = parseBallLimits(readFile(limitsPath));
    } else if (std::filesystem::exists(legacyPath, error)) {
        ballLimits = parseBallLimits(readFile(legacyPath));
    } else {
        ballLimits.clear();
    }
    benchmarkRecord = parseBenchmarkRecord(readFile(folder / benchmarkFileName));
}

void MachineProfile::saveBallLimits() const {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream(folder / limitsFileName, std::ios::trunc) << serializeBallLimits(ballLimits);
}

void MachineProfile::rememberBallLimit(BackendKind backend, double targetFps, BallCount limit) {
    ballLimits[backend] = {targetFps, limit};
    saveBallLimits();
}

std::optional<MachineProfile::MeasuredLimit> MachineProfile::ballLimit(BackendKind backend) const {
    const auto found = ballLimits.find(backend);
    return found == ballLimits.end() ? std::nullopt : std::optional<MeasuredLimit>(found->second);
}

void MachineProfile::rememberBenchmark(BenchmarkRecord record) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream(folder / benchmarkFileName, std::ios::trunc) << serializeBenchmarkRecord(record);
    benchmarkRecord = std::move(record);
}
