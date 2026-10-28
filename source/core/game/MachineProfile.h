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

// One "<backend> <target fps> <balls>" line per backend. The parser also takes the tab-separated
// "limit\t<backend>\t<balls>\t<fps>" lines the Unity front end wrote before the profile moved into the core.
std::string serializeBallLimits(const MeasuredBallLimits& limits);
MeasuredBallLimits parseBallLimits(std::string_view text);
