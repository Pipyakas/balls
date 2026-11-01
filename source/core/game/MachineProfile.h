#pragma once

#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class MachineProfile {
public:
    MachineProfile() = default;
    explicit MachineProfile(std::filesystem::path directory) : folder(std::move(directory)) {}

    void load();

    const std::optional<BenchmarkRecord>& benchmark() const { return benchmarkRecord; }
    void rememberBenchmark(BenchmarkRecord record);

    const std::filesystem::path& directory() const { return folder; }

private:
    std::filesystem::path folder;
    std::optional<BenchmarkRecord> benchmarkRecord;
};

inline constexpr std::string_view targetBenchmarkFileName = "target-benchmark.txt";

std::optional<BenchmarkCandidate> targetMachineWinner(const std::filesystem::path& recordFile);

BackendKind chooseStartupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault,
                                 const std::filesystem::path& targetRecordFile);
