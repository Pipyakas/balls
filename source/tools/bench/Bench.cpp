#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"
#include "sim/BackendSelection.h"
#include "sim/DeviceComposer.h"
#include "sim/DeviceInventory.h"
#include "sim/HybridBackend.h"
#include "sim/PhysicsAccuracy.h"
#include "sim/Simulation.h"

#include <emmintrin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// One backend to measure: a kind, or a set of device picks ("AVX2@cpu+CUDA@gpu0") that runs as one kind.
struct BenchTarget {
    BackendKind kind = BackendKind::Scalar;
    std::vector<BackendPick> picks;
};

std::vector<BenchTarget> allTargets() {
    std::vector<BenchTarget> targets;
    for (const BackendKind kind : allBackendKinds) {
        targets.push_back({kind, {}});
    }
    return targets;
}

// Makes the target's picks the process-wide ones, so createBackend(kind) builds exactly this target.
BackendKind applyTarget(const BenchTarget& target) {
    setBackendPicks(target.picks);
    return target.kind;
}

// "CUDA", or the picks a composed or device-pinned run used: "Hybrid[AVX2@cpu+CUDA@gpu0]".
std::string targetLabel(BackendKind kind) {
    const std::vector<BackendPick> picks = backendPicks();
    if (kind == BackendKind::Hybrid) {
        return "Hybrid[" + formatBackendPicks(hybridPicks()) + "]";
    }
    if (!picks.empty()) {
        return formatBackendPicks(picks);
    }
    return std::string(backendKindName(kind));
}

struct BenchOptions {
    std::filesystem::path levelDirectory = "data/assets/levels/performance";
    std::string levelName = "DA_1000Bricks";
    std::vector<BenchTarget> backends = allTargets();
    std::vector<size_t> ballCounts{2000, 20000, 200000, 1000000};
    int ticks = 240;
    bool fullSimulation = false;
    bool scatterSimulation = false;
    bool roofline = false;
    bool cpuRoofline = false;
    bool rooflineOnLevel = false;
    bool collisions = false;
    int substeps = 2;
    std::filesystem::path machineBenchmarkOutput;
    bool accuracy = false;
    bool listDevices = false;
    bool visibilityGrid = false;
};

std::vector<size_t> parseCounts(const char* text) {
    std::vector<size_t> counts;
    for (const char* cursor = text; *cursor;) {
        char* end = nullptr;
        counts.push_back(std::strtoull(cursor, &end, 10));
        cursor = *end == ',' ? end + 1 : end;
    }
    return counts;
}

// Comma-separated: "all", kind names ("cuda", "cuda+avx2", "hybrid") or device picks ("avx2@cpu+vulkan@gpu1").
std::vector<BenchTarget> parseBackends(const std::string& text) {
    std::vector<BenchTarget> targets;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item == "all") {
            const std::vector<BenchTarget> all = allTargets();
            targets.insert(targets.end(), all.begin(), all.end());
        } else if (item.find('@') != std::string::npos) {
            if (auto picks = parseBackendPicks(item)) {
                const BackendKind kind = kindForPicks(*picks);
                targets.push_back({kind, std::move(*picks)});
            } else {
                std::fprintf(stderr, "unknown device picks '%s' (see --devices)\n", item.c_str());
            }
        } else if (const auto kind = backendKindFromName(item)) {
            targets.push_back({*kind, {}});
        } else {
            std::fprintf(stderr, "unknown backend '%s'\n", item.c_str());
        }
    }
    return targets;
}

BenchOptions parseOptions(int argumentCount, char** arguments) {
    BenchOptions options;
    for (int i = 1; i < argumentCount; ++i) {
        const std::string argument = arguments[i];
        const char* value = i + 1 < argumentCount ? arguments[i + 1] : "";
        if (argument == "--dir") {
            options.levelDirectory = value;
            ++i;
        } else if (argument == "--level") {
            options.levelName = value;
            ++i;
        } else if (argument == "--backend") {
            options.backends = parseBackends(value);
            ++i;
        } else if (argument == "--balls") {
            options.ballCounts = parseCounts(value);
            ++i;
        } else if (argument == "--ticks") {
            options.ticks = std::atoi(value);
            ++i;
        } else if (argument == "--cpu-roofline") {
            options.cpuRoofline = true;
        } else if (argument == "--roofline") {
            options.roofline = true;
        } else if (argument == "--on-level") {
            options.rooflineOnLevel = true;
        } else if (argument == "--substeps") {
            options.substeps = std::atoi(value);
            ++i;
        } else if (argument == "--full") {
            options.fullSimulation = true;
        } else if (argument == "--scatter") {
            options.scatterSimulation = true;
        } else if (argument == "--collisions") {
            options.collisions = true;
        } else if (argument == "--machine-benchmark") {
            options.machineBenchmarkOutput = value;
            ++i;
        } else if (argument == "--accuracy") {
            options.accuracy = true;
        } else if (argument == "--grid") {
            options.visibilityGrid = true;
        } else if (argument == "--devices") {
            options.listDevices = true;
        }
    }
    return options;
}

BrickLayout emptyBoard(int width, int rows);

void listPhysicalDevices() {
    for (const PhysicalDevice& device : physicalDevices()) {
        std::printf("%-5s %s%s%s\n", device.id.c_str(), device.name.c_str(), device.hasUuid ? "  uuid " : "",
                    device.hasUuid ? uuidText(device.uuid).c_str() : "");
        for (const DeviceBackendOption& option : device.backends) {
            const std::string reason = backendUnavailableReasonOnDevice(option.kind, option.pluginDevice);
            const std::string detail = reason.empty() ? backendDetailOnDevice(option.kind, option.pluginDevice) : reason;
            std::printf("      %-7s #%d %s %s\n", std::string(backendKindName(option.kind)).c_str(), option.pluginDevice,
                        reason.empty() ? "ok  " : "MISS", detail.c_str());
        }
    }
}

// Every backend against ReferenceBackend on the same scenarios: an empty box (positions compared ball by ball) and
// the level with balls launched as in a game and scattered as the probe used to (curves and invariants).
int runAccuracy(const BenchOptions& options, const BrickLayout& level) {
    constexpr size_t boxBalls = 20'000;
    constexpr size_t levelBalls = 20'000;
    constexpr int boxFrames = 600;
    constexpr int levelFrames = 1200;
    std::vector<AccuracyScenario> scenarios{
        {"empty box", emptyBoard(level.width, level.playableHeight), boxBalls, false, boxFrames},
        {"empty box, stepped", emptyBoard(level.width, level.playableHeight), boxBalls, false, boxFrames, 2, 60, false},
        {"level, launched", level, levelBalls, true, levelFrames},
    };
    for (const AccuracyScenario& scenario : scenarios) {
        std::printf("== %s: %zu balls, %d frames\n", scenario.name.c_str(), scenario.balls, scenario.frames);
        const AccuracyRun reference = runAccuracyScenario(createReferenceBackend(), scenario);
        for (const AccuracySample& point : reference.samples) {
            std::printf("   reference frame %4d bricks %4d balls %6zu hits %8llu\n", point.frame, point.bricks,
                        point.balls, point.hits);
        }
        constexpr double finerSubStep = 0.005;
        const AccuracyRun finer = runAccuracyScenario(createReferenceBackend(finerSubStep), scenario);
        std::printf("   %s   <- noise floor: reference at half the sub-step\n",
                    describeAccuracy("Ref/2", compareToReference(finer, reference)).c_str());
        for (const BenchTarget& target : options.backends) {
            const BackendKind kind = applyTarget(target);
            if (!isBackendAvailable(kind)) {
                continue;
            }
            const AccuracyRun run = runAccuracyScenario(createBackend(kind), scenario);
            if (!run.fault.empty()) {
                std::printf("   %-9s fault: %s\n", targetLabel(kind).c_str(), run.fault.c_str());
                continue;
            }
            std::printf("   %s\n", describeAccuracy(targetLabel(kind), compareToReference(run, reference)).c_str());
            std::string hitsCurve;
            for (size_t i = 0; i < std::min(run.samples.size(), reference.samples.size()); ++i) {
                hitsCurve += std::format(" {:+d}", static_cast<long long>(run.samples[i].hits) -
                                                       static_cast<long long>(reference.samples[i].hits));
            }
            std::printf("             hits minus reference per sample:%s\n", hitsCurve.c_str());
        }
    }
    return 0;
}

// The game's Settings > Run machine benchmark, without a window: same workload, same candidates, same winner rule.
// The record it writes is what the game ships as the scoring machine's default (data/assets/target-benchmark.txt).
int runMachineBenchmark(const BenchOptions& options, BrickLayout level) {
    constexpr auto pollInterval = std::chrono::milliseconds(200);
    BenchmarkWorkload workload;
    workload.level = std::move(level);
    BackendBenchmark benchmark(std::move(workload), benchmarkCandidates());
    benchmark.start();
    while (!benchmark.finished()) {
        std::this_thread::sleep_for(pollInterval);
    }
    const std::vector<CandidateResult> results = benchmark.results();
    for (const CandidateResult& result : results) {
        std::printf("%-24s %-9s %10.1f M ball-steps/s  ~%-8s balls at 60 FPS (capacity %s) %s\n",
                    result.candidate.label.c_str(), result.status == CandidateStatus::Done ? "done" : "not done",
                    result.ballStepsPerSecond / 1e6, shortBallCount(result.estimatedBalls).c_str(),
                    shortBallCount(result.ballCapacity).c_str(), result.failure.c_str());
    }
    const BenchmarkCandidate winner = chooseDefaultBackend(results, isBackendAvailable);
    std::printf("winner: %s\n", winner.label.c_str());
    const BenchmarkRecord record{currentHardwareFingerprint(""), results, winner, true};
    std::ofstream output(options.machineBenchmarkOutput, std::ios::binary);
    output << serializeBenchmarkRecord(record);
    if (!output) {
        std::fprintf(stderr, "could not write %s\n", options.machineBenchmarkOutput.string().c_str());
        return 1;
    }
    std::printf("wrote %s\n", options.machineBenchmarkOutput.string().c_str());
    return 0;
}

std::vector<BallSpawn> scatterBalls(const BrickBoard& board, size_t count, float speed) {
    std::mt19937 random(1234u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDistribution(0, ballColorCount - 1);
    std::vector<BallSpawn> balls;
    balls.reserve(count);
    while (balls.size() < count) {
        const float x = unit(random) * static_cast<float>(board.columns());
        const float y = unit(random) * static_cast<float>(board.rows());
        const int column = static_cast<int>(x);
        const int row = static_cast<int>(y);
        if (board.brickAt(column, row) >= 0) {
            continue;
        }
        const float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
        balls.push_back({x, y, std::cos(angle) * speed, std::sin(angle) * speed, colorDistribution(random)});
    }
    return balls;
}

void printComposerParts(const DeviceComposer& composer) {
    for (size_t part = 0; part < composer.partCount(); ++part) {
        std::printf("        %-12s share %5.1f%%  balls %10zu  step %7.3f ms\n", composer.partLabel(part).c_str(),
                    100.0 * static_cast<double>(composer.partBallCount(part)) /
                        static_cast<double>(std::max<size_t>(composer.ballCount(), 1)),
                    composer.partBallCount(part), composer.partStepMs(part));
        if (composer.partParked(part)) {
            std::printf("        %-12s parked: slower composed than without it\n", composer.partLabel(part).c_str());
        }
    }
    std::printf("        brick budgets %.3f ms in the last tick\n", composer.brickBudgetMs());
}

void runRawStep(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks, bool collisions,
                bool visibilityGrid) {
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    if (visibilityGrid) {
        const float slotSize = simulation.tuning().ballRadius * visibilitySlotsPerRadius;
        simulation.backend().setVisibilityGrid(
            visibilitySlots(static_cast<float>(simulation.board().columns()), slotSize),
            visibilitySlots(static_cast<float>(simulation.board().rows()), slotSize));
    }
    simulation.setBallCollisions(collisions);
    PhysicsBackend& backend = simulation.backend();
    const auto balls = scatterBalls(simulation.board(), ballCount, simulation.tuning().ballSpeed);
    backend.addBalls(balls);
    const StepParams params = simulation.stepParams();
    std::vector<BallHit> hits;
    for (int warmup = 0; warmup < 10; ++warmup) {
        hits.clear();
        backend.step(params, hits);
    }
    size_t eventTotal = 0;
    const bool resolvesOnDevice = backend.resolvesHitsOnDevice();
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        hits.clear();
        backend.step(params, hits);
        if (resolvesOnDevice) {
            eventTotal += static_cast<size_t>(backend.deviceHitResults().killed);
        } else {
            eventTotal += hits.size();
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double msPerTick = seconds * 1000.0 / ticks;
    const double ballStepsPerSecond = static_cast<double>(ballCount) * ticks / seconds;
    std::printf("%-7s collisions=%d balls=%9zu  %8.3f ms/tick  %8.1f M ball-steps/s  %s/tick=%zu\n",
                targetLabel(kind).c_str(), collisions ? 1 : 0, ballCount, msPerTick,
                ballStepsPerSecond / 1e6, resolvesOnDevice ? "kills" : "hits", eventTotal / static_cast<size_t>(ticks));
    if (const auto* composer = dynamic_cast<const DeviceComposer*>(&backend)) {
        printComposerParts(*composer);
    }
}

void runFullSimulation(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks,
                       bool collisions) {
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    simulation.setBallCollisions(collisions);
    const size_t perColor = ballCount / ballColorCount;
    for (BallColor color : allBallColors) {
        simulation.launchBalls(color, static_cast<int>(perColor));
    }
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        simulation.tick();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-7s collisions=%d full balls=%9zu  %8.3f ms/tick  remaining balls=%zu bricks=%d\n",
                targetLabel(kind).c_str(), collisions ? 1 : 0, ballCount,
                seconds * 1000.0 / ticks, simulation.backend().ballCount(),
                simulation.board().aliveBrickCount());
}

void runScatteredSimulation(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks,
                            int substeps, bool collisions) {
    constexpr int warmupTicks = 30;
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    simulation.setBallCollisions(collisions);
    const size_t perColor = ballCount / ballColorCount;
    for (BallColor color : allBallColors) {
        simulation.scatterBalls(color, static_cast<BallCount>(perColor));
    }
    for (int tick = 0; tick < warmupTicks; ++tick) {
        simulation.tick(substeps);
    }
    const int bricksBefore = simulation.board().aliveBrickCount();
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        simulation.tick(substeps);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf(
        "%-7s collisions=%d scatter balls=%9zu  %8.3f ms/tick  %8.1f M ball-steps/s  bricks %d -> %d\n",
        targetLabel(kind).c_str(), collisions ? 1 : 0, ballCount, seconds * 1000.0 / ticks,
        static_cast<double>(ballCount) * ticks / seconds / 1e6, bricksBefore,
        simulation.board().aliveBrickCount());
    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        std::printf("        cpu share %5.1f%%  cpu step %6.3f ms  gpu step %6.3f ms\n",
                    100.0 * static_cast<double>(hybrid->cpuBallCount()) / static_cast<double>(hybrid->ballCount()),
                    hybrid->cpuStepMs(), hybrid->gpuStepMs());
    }
    if (const auto* composer = dynamic_cast<const DeviceComposer*>(&simulation.backend())) {
        printComposerParts(*composer);
    }
}

BrickLayout emptyBoard(int width, int rows) {
    BrickLayout layout;
    layout.width = width;
    layout.height = 1;
    layout.playableHeight = rows;
    layout.cellIds.assign(static_cast<size_t>(width), emptyBrickId);
    return layout;
}

void runRoofline(const BenchOptions& options, const BrickLayout* level) {
    constexpr size_t copyBufferBytes = size_t{512} << 20;
    constexpr int boardWidth = 30;
    constexpr int boardRows = 38;
    const GpuRoofline roofline = measureGpuRoofline(copyBufferBytes);
    std::printf("GPU: %d SMs @ %.0f MHz, memory %d-bit @ %.0f MHz\n", roofline.multiprocessors,
                roofline.coreClockMegahertz, roofline.memoryBusBits, roofline.memoryClockMegahertz);
    std::printf("bandwidth: theoretical %.1f GB/s, device copy %.1f GB/s (%.0f%% of theoretical)\n",
                roofline.theoreticalGigabytesPerSecond, roofline.copyGigabytesPerSecond,
                100.0 * roofline.copyGigabytesPerSecond / roofline.theoreticalGigabytesPerSecond);
    const size_t bytesPerBall = gpuStreamedBytesPerBall(false);
    for (size_t ballCount : options.ballCounts) {
        Simulation simulation(createBackend(BackendKind::Cuda));
        simulation.load(level ? *level : emptyBoard(boardWidth, boardRows));
        PhysicsBackend& backend = simulation.backend();
        backend.addBalls(scatterBalls(simulation.board(), ballCount, simulation.tuning().ballSpeed));
        const StepParams params = simulation.stepParams(options.substeps);
        std::vector<BallHit> hits;
        constexpr int warmupTicks = 5;
        for (int tick = 0; tick < warmupTicks; ++tick) {
            backend.step(params, hits);
        }
        double kernelMs = 0.0;
        for (int tick = 0; tick < options.ticks; ++tick) {
            backend.step(params, hits);
            kernelMs += backend.deviceStepMs();
        }
        kernelMs /= options.ticks;
        const double achievedGigabytesPerSecond =
            static_cast<double>(ballCount * bytesPerBall) / (kernelMs * 1.0e6);
        std::printf("balls=%10zu substeps=%d kernel %7.3f ms  %6.1f GB/s = %5.1f%% of copy  %6.0f M "
                    "ball-substeps/s\n",
                    ballCount, options.substeps, kernelMs, achievedGigabytesPerSecond,
                    100.0 * achievedGigabytesPerSecond / roofline.copyGigabytesPerSecond,
                    static_cast<double>(ballCount) * options.substeps / (kernelMs * 1.0e3));
    }
}

void runCpuRoofline(size_t ballCount, int passes) {
    constexpr size_t bytesTouchedPerBall = 4 * sizeof(uint32_t);
    std::vector<uint32_t> position(ballCount, 1u);
    std::vector<uint32_t> state(ballCount, 2u);
    std::vector<uint32_t> packed(ballCount, 0u);
    const size_t threads = std::max(1u, std::thread::hardware_concurrency());
    constexpr size_t ballsPerVector = sizeof(__m128i) / sizeof(uint32_t);
    constexpr size_t ballsPerCacheLine = 16;
    const size_t perThread = (ballCount / threads + ballsPerCacheLine - 1) / ballsPerCacheLine * ballsPerCacheLine;
    const auto pass = [&] {
        std::vector<std::jthread> workers;
        for (size_t thread = 0; thread < threads; ++thread) {
            workers.emplace_back([&, thread] {
                const size_t begin = std::min(thread * perThread, ballCount);
                const size_t end = thread + 1 == threads ? ballCount : std::min(begin + perThread, ballCount);
                size_t ball = begin;
                for (; ball + ballsPerVector <= end; ball += ballsPerVector) {
                    auto* positionVector = reinterpret_cast<__m128i*>(position.data() + ball);
                    const __m128i stateVector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data() + ball));
                    const __m128i moved = _mm_add_epi32(_mm_loadu_si128(positionVector), stateVector);
                    _mm_storeu_si128(positionVector, moved);
                    _mm_stream_si128(reinterpret_cast<__m128i*>(packed.data() + ball), _mm_xor_si128(moved, stateVector));
                }
                for (; ball < end; ++ball) {
                    position[ball] += state[ball];
                    packed[ball] = position[ball] ^ state[ball];
                }
            });
        }
    };
    pass();
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < passes; ++index) {
        pass();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double ballsPerSecond = static_cast<double>(ballCount) * passes / seconds;
    std::printf("CPU stream: %zu threads, read position+state, write position+packed: %.1f M balls/s = %.1f GB/s "
                "(%zu bytes per ball, packed written with streaming stores)\n",
                threads, ballsPerSecond / 1e6, ballsPerSecond * bytesTouchedPerBall / 1e9, bytesTouchedPerBall);
}

} // namespace

int main(int argumentCount, char** arguments) {
#if defined(_MSC_VER)
    char* computeGpu = nullptr;
    size_t computeGpuLength = 0;
    if (_dupenv_s(&computeGpu, &computeGpuLength, "BALLS_COMPUTE_GPU") == 0 && computeGpu) {
        setComputeGpuPreference(computeGpu);
        std::free(computeGpu);
    }
#else
    if (const char* computeGpu = std::getenv("BALLS_COMPUTE_GPU")) {
        setComputeGpuPreference(computeGpu);
    }
#endif
    const BenchOptions options = parseOptions(argumentCount, arguments);
    if (options.listDevices) {
        listPhysicalDevices();
        return 0;
    }
    if (options.cpuRoofline) {
        runCpuRoofline(options.ballCounts.empty() ? size_t{96000000} : options.ballCounts.front(),
                       std::max(options.ticks, 1));
        return 0;
    }
    if (options.roofline) {
        const bool onLevel = options.rooflineOnLevel;
        auto rooflineLevel = onLevel ? loadLevel(levelFilesFor(options.levelDirectory, options.levelName))
                                     : Result<LevelData>(Error{"empty board"});
        std::printf("board: %s\n", onLevel ? options.levelName.c_str() : "empty");
        runRoofline(options, rooflineLevel ? &rooflineLevel->bricks : nullptr);
        return 0;
    }
    auto level = loadLevel(levelFilesFor(options.levelDirectory, options.levelName));
    if (!level) {
        std::fprintf(stderr, "level load failed: %s\n", level.error().c_str());
        return 1;
    }
    if (!options.machineBenchmarkOutput.empty()) {
        return runMachineBenchmark(options, std::move(level->bricks));
    }
    if (options.accuracy) {
        return runAccuracy(options, level->bricks);
    }
    for (const BenchTarget& target : options.backends) {
        const BackendKind kind = applyTarget(target);
        if (!isBackendAvailable(kind)) {
            std::printf("%-7s unavailable: %s\n", targetLabel(kind).c_str(),
                        backendUnavailableReason(kind).c_str());
            continue;
        }
        for (size_t count : options.ballCounts) {
            if (options.scatterSimulation) {
                runScatteredSimulation(level->bricks, kind, count, options.ticks, options.substeps,
                                       options.collisions);
            } else if (options.fullSimulation) {
                runFullSimulation(level->bricks, kind, count, options.ticks, options.collisions);
            } else {
                runRawStep(level->bricks, kind, count, options.ticks, options.collisions, options.visibilityGrid);
            }
        }
    }
    return 0;
}
