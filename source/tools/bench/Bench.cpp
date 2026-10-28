#include "game/SpawnPacer.h"
#include "level/EdgeCaseLevels.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWriter.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"
#include "sim/EdgeCaseSweep.h"
#include "sim/HybridBackend.h"
#include "sim/PhysicsAccuracy.h"
#include "sim/Simulation.h"

#include <emmintrin.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

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
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchOptions {
    std::filesystem::path levelDirectory = "data/assets/levels/performance";
    std::string levelName = "DA_1000Bricks";
    std::vector<BackendKind> backends{allBackendKinds.begin(), allBackendKinds.end()};
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
    bool edgeCases = false;
    std::string edgeLayouts = "all";
    std::string edgeSizes = "all";
    double edgeCaseSeconds = 40.0;
    std::filesystem::path edgeOutput;
    int traceFrames = 0;
    BallCount traceSpawnTo = 0;
    std::filesystem::path edgeLevelDirectory;
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

std::vector<BackendKind> parseBackends(const std::string& text) {
    std::vector<BackendKind> kinds;
    std::string loweredText = text;
    for (char& c : loweredText) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (BackendKind kind : allBackendKinds) {
        std::string name(backendKindName(kind));
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (loweredText == "all" || loweredText.find(name) != std::string::npos) {
            kinds.push_back(kind);
        }
    }
    return kinds;
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
        } else if (argument == "--edge-cases") {
            options.edgeCases = true;
        } else if (argument == "--layouts") {
            options.edgeLayouts = value;
            ++i;
        } else if (argument == "--sizes") {
            options.edgeSizes = value;
            ++i;
        } else if (argument == "--case-seconds") {
            options.edgeCaseSeconds = std::atof(value);
            ++i;
        } else if (argument == "--write-edge-levels") {
            options.edgeLevelDirectory = value;
            ++i;
        } else if (argument == "--trace-spawn-to") {
            options.traceSpawnTo = std::strtoull(value, nullptr, 10);
            ++i;
        } else if (argument == "--trace-frames") {
            options.traceFrames = std::atoi(value);
            ++i;
        } else if (argument == "--out") {
            options.edgeOutput = value;
            ++i;
        }
    }
    return options;
}

BrickLayout emptyBoard(int width, int rows);

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
        for (BackendKind kind : options.backends) {
            if (!isBackendAvailable(kind)) {
                continue;
            }
            const AccuracyRun run = runAccuracyScenario(createBackend(kind), scenario);
            if (!run.fault.empty()) {
                std::printf("   %-9s fault: %s\n", std::string(backendKindName(kind)).c_str(), run.fault.c_str());
                continue;
            }
            std::printf("   %s\n", describeAccuracy(backendKindName(kind), compareToReference(run, reference)).c_str());
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

bool listed(const std::string& list, const std::string& name) {
    return list == "all" || (',' + list + ',').find(',' + name + ',') != std::string::npos;
}

// Saves the generated boards as level files, so the game lists them under Performance.
int writeEdgeLevels(const BenchOptions& options) {
    for (const EdgeBoardSize size : edgeBoardSizes()) {
        if (!listed(options.edgeSizes, std::format("{}x{}", size.width, size.height))) {
            continue;
        }
        for (const EdgeLayout layout : allEdgeLayouts()) {
            if (!listed(options.edgeLayouts, edgeLayoutName(layout))) {
                continue;
            }
            const LevelData level = generateEdgeLevel(layout, size);
            const Result<void> saved = saveLevel(level, levelFilesFor(options.edgeLevelDirectory, level.name));
            if (!saved) {
                std::fprintf(stderr, "%s: %s\n", level.name.c_str(), saved.error().c_str());
                return 1;
            }
            std::printf("wrote %s\n", level.name.c_str());
        }
    }
    return 0;
}

// Available physical memory and the process's page faults so far, to see a fill approach the swap line.
std::string memoryReport() {
#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return std::format("  available {:.1f} GB  working set {:.1f} GB  page faults {}",
                       static_cast<double>(memory.ullAvailPhys) / 1e9, static_cast<double>(counters.WorkingSetSize) / 1e9,
                       counters.PageFaultCount);
#else
    return {};
#endif
}

// One generated board, one backend, one ball count: every frame's time and live balls, to find where spikes come from.
int traceEdgeCases(const BenchOptions& options) {
    for (const EdgeBoardSize size : edgeBoardSizes()) {
        if (!listed(options.edgeSizes, std::format("{}x{}", size.width, size.height))) {
            continue;
        }
        for (const EdgeLayout layout : allEdgeLayouts()) {
            if (!listed(options.edgeLayouts, edgeLayoutName(layout))) {
                continue;
            }
            for (BackendKind kind : options.backends) {
                BackendHandle backend = createBackend(kind);
                if (!backend) {
                    continue;
                }
                Simulation simulation(std::move(backend));
                simulation.load(generateEdgeLayout(layout, size));
                const float slotSize = simulation.tuning().ballRadius * visibilitySlotsPerRadius;
                simulation.backend().setVisibilityGrid(
                    visibilitySlots(static_cast<float>(simulation.board().columns()), slotSize),
                    visibilitySlots(static_cast<float>(simulation.board().rows()), slotSize));
                const size_t balls = options.ballCounts.empty() ? size_t{1} << 22 : options.ballCounts.front();
                for (const BallColor color : allBallColors) {
                    simulation.launchBalls(color, static_cast<BallCount>(balls / ballColorCount));
                }
                std::printf("== %s %s %zu balls\n", edgeCaseName(layout, size).c_str(),
                            std::string(backendKindName(kind)).c_str(), balls);
                SpawnPacer pacer;
                int colorIndex = 0;
                for (int frame = 0; frame < options.traceFrames; ++frame) {
                    using Clock = std::chrono::steady_clock;
                    const auto start = Clock::now();
                    const BallCount live = simulation.liveBallCount();
                    const BallCount wanted = options.traceSpawnTo > live ? options.traceSpawnTo - live : 0;
                    const BallCount launched = std::min(wanted, pacer.allowance());
                    simulation.launchBalls(ballColorFromIndex(colorIndex++ % ballColorCount), launched);
                    simulation.flushPendingSpawns();
                    const auto spawned = Clock::now();
                    pacer.recordSpawn(launched, std::chrono::duration<double>(spawned - start).count());
                    simulation.tick(options.substeps);
                    const double spawnMs = std::chrono::duration<double, std::milli>(spawned - start).count();
                    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    pacer.reportFrame(ms / 1e3, simulation.liveBallCount());
                    std::string split = frame % 50 == 0 ? memoryReport() : std::string();
                    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
                        split += std::format("  cpu {:10} balls {:6.2f} ms  gpu {:6.2f} ms", hybrid->cpuBallCount(),
                                             hybrid->cpuStepMs(), hybrid->gpuStepMs());
                    }
                    std::printf("frame %4d %8.2f ms  spawn %6.2f ms (%9llu)  live %10llu  device %6.2f ms%s\n", frame,
                                ms, spawnMs, static_cast<unsigned long long>(launched),
                                static_cast<unsigned long long>(simulation.liveBallCount()),
                                simulation.backend().deviceStepMs(), split.c_str());
                }
            }
        }
    }
    return 0;
}

// Every generated board shape and size against every backend, from small to as big as the machine holds; one line
// per pair, also written as tab-separated rows to --out.
int runEdgeCases(const BenchOptions& options) {
    EdgeSweepSettings settings;
    settings.caseSeconds = options.edgeCaseSeconds;
    std::ofstream output;
    if (!options.edgeOutput.empty()) {
        output.open(options.edgeOutput, std::ios::binary);
        output << "case\tbackend\tballs_at_60\twindow_ms\tworst_ms\tlaunched\tbricks_left\tlaunch_ns_per_ball\tstop\t"
                  "seconds\n";
    }
    for (const EdgeBoardSize size : edgeBoardSizes()) {
        if (!listed(options.edgeSizes, std::format("{}x{}", size.width, size.height))) {
            continue;
        }
        for (const EdgeLayout layout : allEdgeLayouts()) {
            if (!listed(options.edgeLayouts, edgeLayoutName(layout))) {
                continue;
            }
            const std::string name = edgeCaseName(layout, size);
            const BrickLayout bricks = generateEdgeLayout(layout, size);
            for (BackendKind kind : options.backends) {
                if (!isBackendAvailable(kind)) {
                    continue;
                }
                const EdgeSweepResult result = sweepEdgeCase(bricks, kind, settings);
                const std::string backendName(backendKindName(kind));
                std::printf("%-28s %-7s ~%-7s at 60 FPS  (last window %.1f ms, worst frame %.1f ms, %s launched)  "
                            "bricks %d  launch %.0f ns/ball  stop: %s  %.0f s\n",
                            name.c_str(), backendName.c_str(), shortBallCount(result.ballsAtBudget).c_str(),
                            result.windowFrameMs, result.worstFrameMs,
                            shortBallCount(static_cast<double>(result.launchedBalls)).c_str(), result.aliveBricks,
                            result.launchNanosecondsPerBall, result.stopReason.c_str(), result.seconds);
                std::fflush(stdout);
                if (output.is_open()) {
                    output << std::format("{}\t{}\t{:.0f}\t{:.2f}\t{:.2f}\t{}\t{}\t{:.1f}\t{}\t{:.1f}\n", name,
                                          backendName, result.ballsAtBudget, result.windowFrameMs, result.worstFrameMs,
                                          result.launchedBalls, result.aliveBricks, result.launchNanosecondsPerBall,
                                          result.stopReason, result.seconds);
                    output.flush();
                }
            }
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

void runRawStep(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks, bool collisions) {
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
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
                std::string(backendKindName(kind)).c_str(), collisions ? 1 : 0, ballCount, msPerTick,
                ballStepsPerSecond / 1e6, resolvesOnDevice ? "kills" : "hits", eventTotal / static_cast<size_t>(ticks));
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
                std::string(backendKindName(kind)).c_str(), collisions ? 1 : 0, ballCount,
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
        std::string(backendKindName(kind)).c_str(), collisions ? 1 : 0, ballCount, seconds * 1000.0 / ticks,
        static_cast<double>(ballCount) * ticks / seconds / 1e6, bricksBefore,
        simulation.board().aliveBrickCount());
    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        std::printf("        cpu share %5.1f%%  cpu step %6.3f ms  gpu step %6.3f ms\n",
                    100.0 * static_cast<double>(hybrid->cpuBallCount()) / static_cast<double>(hybrid->ballCount()),
                    hybrid->cpuStepMs(), hybrid->gpuStepMs());
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
    if (!options.edgeLevelDirectory.empty()) {
        return writeEdgeLevels(options);
    }
    if (options.edgeCases) {
        return options.traceFrames > 0 ? traceEdgeCases(options) : runEdgeCases(options);
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
    for (BackendKind kind : options.backends) {
        if (!isBackendAvailable(kind)) {
            std::printf("%-7s unavailable: %s\n", std::string(backendKindName(kind)).c_str(),
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
                runRawStep(level->bricks, kind, count, options.ticks, options.collisions);
            }
        }
    }
    return 0;
}
