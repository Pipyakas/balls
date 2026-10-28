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
// scores no longer describe this machine until Settings > Run machine benchmark measures it again.
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
    // Device picks ("AVX2@cpu+CUDA@gpu0", formatBackendPicks) the candidate runs with; empty runs the kind on its
    // default device.
    std::string picks;

    bool operator==(const BenchmarkCandidate&) const = default;
};

enum class CandidateStatus { Pending, Running, Done, Failed, Unavailable };

struct CandidateResult {
    BenchmarkCandidate candidate;
    CandidateStatus status = CandidateStatus::Pending;
    double ballStepsPerSecond = 0.0;
    // Balls this backend could hold at the target frame rate: its frame time fitted over two ball counts and
    // extended to the frame budget, capped by ballCapacityLimit(). This, not speed at one count, picks the default,
    // since the Performance score is the most balls at 60 FPS.
    double estimatedBalls = 0.0;
    double ballCapacity = 0.0;
    double progress = 0.0;  // 0..1 while running
    std::string failure;
};

struct BenchmarkRecord {
    HardwareFingerprint fingerprint;
    std::vector<CandidateResult> results;
    std::optional<BenchmarkCandidate> winner;
    // False when the player skipped before every candidate finished: the winner among the finished ones is used.
    bool complete = false;
};

// Every selectable kind on its default device, every other device each kind can run on, and Hybrid over every
// set of two or more devices (each running its fastest available backend).
std::vector<BenchmarkCandidate> benchmarkCandidates();

bool benchmarkNeeded(const std::optional<BenchmarkRecord>& stored, const HardwareFingerprint& current);
// The finished candidate with the highest estimated ball count, speed breaking ties.
std::optional<size_t> bestCandidate(std::span<const CandidateResult> results);
// "7.1 B", "390 M", "12 k": how the benchmark shows ball counts.
std::string shortBallCount(double balls);
// The winner, or when nothing finished, AVX2 if it is usable here, else Scalar.
BenchmarkCandidate chooseDefaultBackend(std::span<const CandidateResult> results,
                                        const std::function<bool(BackendKind)>& isAvailable);

// Fits frame time = fixed + perBall * balls through two measurements and returns the count whose frame takes
// frameBudgetSeconds, capped by capacity. A backend whose frame barely grows with the count (balls flying freely)
// is limited by capacity alone.
double estimateBallsAtTarget(double smallBalls, double smallFrameSeconds, double largeBalls, double largeFrameSeconds,
                             double frameBudgetSeconds, double capacity);

std::string serializeBenchmarkRecord(const BenchmarkRecord& record);
std::optional<BenchmarkRecord> parseBenchmarkRecord(std::string_view text);

struct BenchmarkWorkload {
    BrickLayout level;
    size_t balls = 1'000'000;
    // The second count the frame time is measured at, to separate per-ball cost from per-frame cost.
    size_t largerBalls = 4'000'000;
    int warmupTicks = 30;
    int warmupFramesAfterGrowth = 10;
    double measuredSeconds = 0.5;
    double targetFps = 60.0;
    // Share of the frame the physics may take; the rest is left to rendering and the game.
    double frameBudgetShare = 0.8;
    int ticksPerFrame = 2;
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
