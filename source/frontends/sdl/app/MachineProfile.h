#pragma once

#include "common/BallCount.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <map>
#include <optional>

class MachineProfile {
public:
    void load();
    struct MeasuredLimit {
        double targetFps = 0.0;
        BallCount balls = 0;
    };

    void rememberBallLimit(BackendKind backend, double targetFps, BallCount limit);
    std::optional<MeasuredLimit> ballLimit(BackendKind backend) const;

    const std::optional<BenchmarkRecord>& benchmark() const { return benchmarkRecord; }
    void rememberBenchmark(BenchmarkRecord record);

private:
    std::filesystem::path filePath() const;
    void save() const;
    std::filesystem::path benchmarkFilePath() const;

    std::map<BackendKind, MeasuredLimit> ballLimits;
    std::optional<BenchmarkRecord> benchmarkRecord;
};
