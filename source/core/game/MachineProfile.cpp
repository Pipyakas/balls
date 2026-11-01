#include "game/MachineProfile.h"

#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"

#include <fstream>
#include <sstream>

namespace {

constexpr const char* benchmarkFileName = "benchmark.txt";

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

}

void MachineProfile::load() {
    benchmarkRecord = parseBenchmarkRecord(readFile(folder / benchmarkFileName));
}

void MachineProfile::rememberBenchmark(BenchmarkRecord record) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream(folder / benchmarkFileName, std::ios::trunc) << serializeBenchmarkRecord(record);
    benchmarkRecord = std::move(record);
}

std::optional<BenchmarkCandidate> targetMachineWinner(const std::filesystem::path& recordFile) {
    const std::optional<BenchmarkRecord> record = parseBenchmarkRecord(readFile(recordFile));
    return record ? record->winner : std::nullopt;
}

BackendKind chooseStartupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault,
                                 const std::filesystem::path& targetRecordFile) {
    if (commandLine && isBackendAvailable(*commandLine)) {
        return *commandLine;
    }
    const auto usable = [](BackendKind kind) { return isBackendAvailable(kind) && isBackendSelectable(kind); };
    if (const auto saved = parseSavedBackendKind(settingsDefault); saved && usable(offeredBackend(*saved))) {
        return offeredBackend(*saved);
    }
    if (usable(BackendKind::Hybrid)) {
        return BackendKind::Hybrid;
    }
    if (const auto target = targetMachineWinner(targetRecordFile); target && usable(offeredBackend(target->kind))) {
        return offeredBackend(target->kind);
    }
    return fastestAvailableCpuBackend();
}
