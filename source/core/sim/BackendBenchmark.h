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

// What the machine benchmark ran on. A change in any field (new GPU, driver update, game update) means the stored
// scores no longer describe this machine, so the benchmark runs again.
struct HardwareFingerprint {
    std::string cpuModel;
    unsigned logicalCores = 0;
    std::vector<std::string> gpus;  // "<name> | <driver version>", in enumeration order
    std::string gameVersion;

    bool operator==(const HardwareFingerprint&) const = default;
};

// CPU brand and thread count from the processor, GPU names and driver versions from the OS (DXGI on Windows).
HardwareFingerprint currentHardwareFingerprint(std::string_view gameVersion);

struct BenchmarkCandidate {
    BackendKind kind = BackendKind::Scalar;
    std::string computeGpu;  // compute adapter preference for backends that take one; empty otherwise
    std::string label;

    bool operator==(const BenchmarkCandidate&) const = default;
};

enum class CandidateStatus { Pending, Running, Done, Failed, Unavailable };

struct CandidateResult {
    BenchmarkCandidate candidate;
    CandidateStatus status = CandidateStatus::Pending;
    double ballStepsPerSecond = 0.0;
    double progress = 0.0;  // 0..1 while running
    std::string failure;
};

struct BenchmarkRecord {
    HardwareFingerprint fingerprint;
    std::vector<CandidateResult> results;
    std::optional<BenchmarkCandidate> winner;
    // False when the player skipped before every candidate finished: the winner is used, and the benchmark runs
    // again on the next launch.
    bool complete = false;
};

// Every backend kind, in the order the Settings list shows them.
std::vector<BenchmarkCandidate> benchmarkCandidates();

bool benchmarkNeeded(const std::optional<BenchmarkRecord>& stored, const HardwareFingerprint& current);
std::optional<size_t> fastestCandidate(std::span<const CandidateResult> results);
// The winner, or when nothing finished, AVX2 if it is usable here, else Scalar.
BenchmarkCandidate chooseDefaultBackend(std::span<const CandidateResult> results,
                                        const std::function<bool(BackendKind)>& isAvailable);

std::string serializeBenchmarkRecord(const BenchmarkRecord& record);
std::optional<BenchmarkRecord> parseBenchmarkRecord(std::string_view text);

struct BenchmarkWorkload {
    BrickLayout level;
    size_t balls = 1'000'000;
    int warmupTicks = 30;
    double measuredSeconds = 1.0;
    double candidateLimitSeconds = 5.0;
    size_t checkBalls = 20'000;
    int checkTicks = 120;
};

// Runs every candidate on a worker thread, one after another, so a UI can poll results() every frame.
class BackendBenchmark {
public:
    BackendBenchmark(BenchmarkWorkload workload, std::vector<BenchmarkCandidate> candidates);
    ~BackendBenchmark();
    BackendBenchmark(const BackendBenchmark&) = delete;
    BackendBenchmark& operator=(const BackendBenchmark&) = delete;

    void start();
    // Stops after the current tick; finished candidates keep their results.
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
