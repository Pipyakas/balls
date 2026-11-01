#pragma once

#include "level/LevelData.h"
#include "sim/BackendFactory.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct HardwareFingerprint {
    std::string cpuModel;
    unsigned logicalCores = 0;
    std::vector<std::string> gpus;
    std::string gameVersion;

    bool operator==(const HardwareFingerprint&) const = default;
};

HardwareFingerprint currentHardwareFingerprint(std::string_view gameVersion);

struct BenchmarkCandidate {
    BackendKind kind = BackendKind::Scalar;
    std::string computeGpu;
    std::string label;

    std::string picks;

    bool operator==(const BenchmarkCandidate&) const = default;
};

enum class CandidateStatus { Pending, Running, Done, Failed, Unavailable };

struct CandidateResult {
    BenchmarkCandidate candidate;
    CandidateStatus status = CandidateStatus::Pending;
    double ballStepsPerSecond = 0.0;

    double estimatedBalls = 0.0;
    double ballCapacity = 0.0;
    double progress = 0.0;
    std::string failure;
};

struct BenchmarkRecord {
    HardwareFingerprint fingerprint;
    std::vector<CandidateResult> results;
    std::optional<BenchmarkCandidate> winner;

    bool complete = false;
};

std::vector<BenchmarkCandidate> benchmarkCandidates();

bool benchmarkNeeded(const std::optional<BenchmarkRecord>& stored, const HardwareFingerprint& current);

std::optional<size_t> bestCandidate(std::span<const CandidateResult> results);

std::string shortBallCount(double balls);

std::string candidateResultText(const CandidateResult& result);

BenchmarkCandidate chooseDefaultBackend(std::span<const CandidateResult> results,
                                        const std::function<bool(BackendKind)>& isAvailable);

double estimateBallsAtTarget(double smallBalls, double smallFrameSeconds, double largeBalls, double largeFrameSeconds,
                             double frameBudgetSeconds, double capacity);

std::string serializeBenchmarkRecord(const BenchmarkRecord& record);
std::optional<BenchmarkRecord> parseBenchmarkRecord(std::string_view text);

struct BenchmarkWorkload {
    BrickLayout level;
    size_t balls = 1'000'000;

    size_t largerBalls = 4'000'000;
    int warmupTicks = 30;
    int warmupFramesAfterGrowth = 10;
    double measuredSeconds = 0.5;
    double targetFps = 60.0;

    double frameBudgetShare = 0.8;
    int ticksPerFrame = 2;
    double candidateLimitSeconds = 5.0;
    size_t checkBalls = 20'000;
    int checkTicks = 120;
};

class BackendBenchmark {
public:
    BackendBenchmark(BenchmarkWorkload workload, std::vector<BenchmarkCandidate> candidates);
    ~BackendBenchmark();
    BackendBenchmark(const BackendBenchmark&) = delete;
    BackendBenchmark& operator=(const BackendBenchmark&) = delete;

    void start();

    void cancel();
    bool finished() const { return done.load(); }
    std::vector<CandidateResult> results() const;

private:
    void run();
    void runCandidate(size_t index, std::optional<long long> referenceHits);
    void update(size_t index, const std::function<void(CandidateResult&)>& change);

    BenchmarkWorkload workload;
    mutable std::mutex resultsMutex;
    std::vector<CandidateResult> candidateResults;
    std::atomic<bool> cancelled = false;
    std::atomic<bool> done = false;
    std::jthread worker;
};
