#include "TestFramework.h"

#include "level/LevelParser.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendSelection.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace {

CandidateResult result(BackendKind kind, CandidateStatus status, double score, std::string failure = {}) {
    CandidateResult entry;
    entry.candidate = {kind, {}, std::string(backendKindName(kind))};
    entry.status = status;
    entry.ballStepsPerSecond = score;
    entry.failure = std::move(failure);
    return entry;
}

HardwareFingerprint sampleFingerprint() {
    return {"12th Gen Intel(R) Core(TM) i5-12400F", 12, {"NVIDIA GeForce GTX 1650 | 32.0.15.9201"}, "0.1.0"};
}

bool everythingAvailable(BackendKind) { return true; }

const char* twoBrickLevel = "[Board]\nWidth 4\nHeight 1\nPlayableHeight 12\n[Layout]\n0 1 -1 -1\n[BrickTypes]\n0 Red 3\n1 Blue 3\n";

}

TEST_CASE("the fastest finished candidate wins and failed ones are never chosen") {
    const std::vector<CandidateResult> results{
        result(BackendKind::Scalar, CandidateStatus::Done, 50e6),
        result(BackendKind::Avx2, CandidateStatus::Done, 900e6),
        result(BackendKind::Cuda, CandidateStatus::Failed, 9e9, "timed out after 5 s"),
        result(BackendKind::Hybrid, CandidateStatus::Unavailable, 0.0),
    };
    EXPECT(bestCandidate(results) == std::optional<size_t>(1));
    EXPECT(chooseDefaultBackend(results, everythingAvailable).kind == BackendKind::Avx2);
}

TEST_CASE("with no finished candidate the default falls back to AVX2, else Scalar") {
    const std::vector<CandidateResult> results{result(BackendKind::Cuda, CandidateStatus::Failed, 0.0, "fault")};
    EXPECT(!bestCandidate(results));
    EXPECT(chooseDefaultBackend(results, everythingAvailable).kind == BackendKind::Avx2);
    EXPECT(chooseDefaultBackend(results, [](BackendKind kind) { return kind != BackendKind::Avx2; }).kind ==
           BackendKind::Scalar);
}

TEST_CASE("the benchmark runs without a result, after a skip, and when the hardware fingerprint changes") {
    const HardwareFingerprint machine = sampleFingerprint();
    EXPECT(benchmarkNeeded(std::nullopt, machine));
    BenchmarkRecord record{machine, {}, std::nullopt, true};
    EXPECT(!benchmarkNeeded(record, machine));
    record.complete = false;
    EXPECT(benchmarkNeeded(record, machine));
    record.complete = true;
    HardwareFingerprint newDriver = machine;
    newDriver.gpus = {"NVIDIA GeForce GTX 1650 | 32.0.15.9300"};
    EXPECT(benchmarkNeeded(record, newDriver));
    HardwareFingerprint newVersion = machine;
    newVersion.gameVersion = "0.2.0";
    EXPECT(benchmarkNeeded(record, newVersion));
}

TEST_CASE("a benchmark record survives a save and load") {
    BenchmarkRecord record{sampleFingerprint(),
                           {result(BackendKind::Scalar, CandidateStatus::Done, 51e6),
                            result(BackendKind::Cuda, CandidateStatus::Failed, 0.0, "no CUDA device")},
                           BenchmarkCandidate{BackendKind::Scalar, {}, "Scalar"},
                           true};
    const auto loaded = parseBenchmarkRecord(serializeBenchmarkRecord(record));
    EXPECT(loaded.has_value());
    EXPECT(loaded->fingerprint == record.fingerprint);
    EXPECT(loaded->complete);
    EXPECT(loaded->winner == record.winner);
    EXPECT(loaded->results.size() == 2);
    EXPECT(loaded->results[1].status == CandidateStatus::Failed);
    EXPECT(loaded->results[1].failure == "no CUDA device");
    EXPECT(loaded->results[0].ballStepsPerSecond == 51e6);
    EXPECT(!parseBenchmarkRecord("not a record"));
}

TEST_CASE("the benchmark runner scores Scalar on a small workload") {
    auto level = parseBrickLayout(twoBrickLevel);
    EXPECT(level.hasValue());
    BenchmarkWorkload workload;
    workload.level = *level;
    workload.balls = 2000;
    workload.largerBalls = 4000;
    workload.warmupTicks = 3;
    workload.warmupFramesAfterGrowth = 2;
    workload.measuredSeconds = 0.05;
    workload.checkBalls = 500;
    workload.checkTicks = 20;
    BackendBenchmark benchmark(workload, {BenchmarkCandidate{BackendKind::Scalar, {}, "Scalar"}});
    benchmark.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!benchmark.finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT(benchmark.finished());
    const std::vector<CandidateResult> results = benchmark.results();
    EXPECT(results.front().status == CandidateStatus::Done);
    EXPECT(results.front().ballStepsPerSecond > 0.0);
    EXPECT(results.front().estimatedBalls > 0.0);
}

TEST_CASE("the backend that can hold the most balls at the target frame rate wins, not the fastest at one count") {
    std::vector<CandidateResult> results{
        result(BackendKind::Cuda, CandidateStatus::Done, 11.6e9),
        result(BackendKind::Hybrid, CandidateStatus::Done, 11.4e9),
    };
    results[0].estimatedBalls = 390e6;
    results[1].estimatedBalls = 7.13e9;
    EXPECT(chooseDefaultBackend(results, everythingAvailable).kind == BackendKind::Hybrid);
}

TEST_CASE("a frame that does not grow with the ball count is limited by capacity, one that does by the budget") {
    constexpr double budget = 0.8 / 60.0;
    EXPECT(estimateBallsAtTarget(1e6, 0.0004, 4e6, 0.0004, budget, 7e9) == 7e9);
    EXPECT(estimateBallsAtTarget(1e6, 0.03, 4e6, 0.03, budget, 7e9) == 0.0);
    EXPECT(estimateBallsAtTarget(1e6, 0.00090, 4e6, 0.00096, budget, 7e9) == 7e9);
    const double stepped = estimateBallsAtTarget(1e6, 0.001, 4e6, 0.004, budget, 7e9);
    EXPECT(std::fabs(stepped - budget / 1e-9) < 1e3);
    EXPECT(estimateBallsAtTarget(1e6, 0.001, 4e6, 0.004, budget, 5e6) == 5e6);
}

TEST_CASE("estimated balls and capacity survive a save and load, and older records still parse") {
    BenchmarkRecord record{sampleFingerprint(), {result(BackendKind::Vulkan, CandidateStatus::Done, 11e9)},
                           BenchmarkCandidate{BackendKind::Vulkan, {}, "Vulkan"}, true};
    record.results[0].estimatedBalls = 7.13e9;
    record.results[0].ballCapacity = 7.2e9;
    const auto loaded = parseBenchmarkRecord(serializeBenchmarkRecord(record));
    EXPECT(loaded && loaded->results.size() == 1);
    EXPECT(loaded->results[0].estimatedBalls == 7.13e9);
    EXPECT(loaded->results[0].ballCapacity == 7.2e9);
    const auto older = parseBenchmarkRecord("cpu\tx\nresult\tCUDA\t\tdone\t8176000000\t\n");
    EXPECT(older && older->results.size() == 1 && older->results[0].estimatedBalls == 0.0);
}

TEST_CASE("backend names parse loosely and the retired CUDA+AVX2 pair's names read as Hybrid") {
    EXPECT(backendKindFromName("CUDA+AVX2") == BackendKind::Hybrid);
    EXPECT(backendKindFromName("cuda-avx2") == BackendKind::Hybrid);
    EXPECT(backendKindFromName("Hybrid") == BackendKind::Hybrid);
    EXPECT(parseStoredBackend("Hybrid")->kind == BackendKind::Hybrid);
    EXPECT(parseStoredBackend("CUDA+AVX2")->kind == BackendKind::Hybrid);
    EXPECT(backendKindFromName("vulkan") == BackendKind::Vulkan);
    EXPECT(!backendKindFromName("cuda+vulkan").has_value());
}
