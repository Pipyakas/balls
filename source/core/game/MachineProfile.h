#pragma once

#include "common/BallCount.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// The game's machine profile: the capacity probe's ball limit per backend and the last machine benchmark. Every front
// end keeps it in one folder of its own choosing (SDL's preference folder, Unreal's Saved/Balls, Unity's
// persistentDataPath) as machine.txt and benchmark.txt.
class MachineProfile {
public:
    struct MeasuredLimit {
        double targetFps = 0.0;
        BallCount balls = 0;
    };

    MachineProfile() = default;
    explicit MachineProfile(std::filesystem::path directory) : folder(std::move(directory)) {}

    // Reads both files; a missing or unreadable file leaves that part empty.
    void load();

    void rememberBallLimit(BackendKind backend, double targetFps, BallCount limit);
    std::optional<MeasuredLimit> ballLimit(BackendKind backend) const;

    const std::optional<BenchmarkRecord>& benchmark() const { return benchmarkRecord; }
    void rememberBenchmark(BenchmarkRecord record);

    const std::filesystem::path& directory() const { return folder; }

private:
    void saveBallLimits() const;

    std::filesystem::path folder;
    std::map<BackendKind, MeasuredLimit> ballLimits;
    std::optional<BenchmarkRecord> benchmarkRecord;
};

using MeasuredBallLimits = std::map<BackendKind, MachineProfile::MeasuredLimit>;

inline constexpr std::string_view targetBenchmarkFileName = "target-benchmark.txt";

// The winner in the target PC's benchmark record (data/assets/target-benchmark.txt, measured with
// balls_bench --machine-benchmark and shipped with the assets), or nothing when there is none.
std::optional<BenchmarkCandidate> targetMachineWinner(const std::filesystem::path& recordFile);

// The backend a new game starts on ("Default backend and machine benchmark"): the command line, then the Settings
// default (this machine's own benchmark winner or the player's pick, kind or device picks), then the target PC's
// winner, then the fastest CPU backend, skipping any that cannot load or is not offered here.
BackendKind chooseStartupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault,
                                 const std::filesystem::path& targetRecordFile);

// One "<backend> <target fps> <balls>" line per backend. The parser also takes the tab-separated
// "limit\t<backend>\t<balls>\t<fps>" lines the Unity front end wrote before the profile moved into the core.
std::string serializeBallLimits(const MeasuredBallLimits& limits);
MeasuredBallLimits parseBallLimits(std::string_view text);
