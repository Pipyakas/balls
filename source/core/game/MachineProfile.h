#pragma once

#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// The game's machine profile: the last machine benchmark. Every front end keeps it in one folder of its own choosing
// (SDL's preference folder, Unreal's Saved/Balls, Unity's persistentDataPath) as benchmark.txt.
class MachineProfile {
public:
    MachineProfile() = default;
    explicit MachineProfile(std::filesystem::path directory) : folder(std::move(directory)) {}

    // Reads the profile files; older files with remembered ball limits are ignored.
    void load();

    const std::optional<BenchmarkRecord>& benchmark() const { return benchmarkRecord; }
    void rememberBenchmark(BenchmarkRecord record);

    const std::filesystem::path& directory() const { return folder; }

private:
    std::filesystem::path folder;
    std::optional<BenchmarkRecord> benchmarkRecord;
};

inline constexpr std::string_view targetBenchmarkFileName = "target-benchmark.txt";

// The winner in the target PC's benchmark record (data/assets/target-benchmark.txt, measured with
// balls_bench --machine-benchmark and shipped with the assets), or nothing when there is none.
std::optional<BenchmarkCandidate> targetMachineWinner(const std::filesystem::path& recordFile);

// The backend a new game starts on ("Default backend and machine benchmark"): the command line, then the Settings
// default (this machine's own benchmark winner or the player's pick, kind or device picks), then Hybrid on its
// default devices, then the target PC's winner, then the fastest CPU backend, skipping any that cannot load or is not
// offered here. A saved or recorded CUDA+AVX2 pair means Hybrid, which replaced it in the menus.
BackendKind chooseStartupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault,
                                 const std::filesystem::path& targetRecordFile);
