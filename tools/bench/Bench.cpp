#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace {

struct BenchOptions {
    std::filesystem::path levelDirectory = "assets/levels/performance";
    std::string levelName = "DA_1000Bricks";
    std::vector<BackendKind> backends{allBackendKinds.begin(), allBackendKinds.end()};
    std::vector<size_t> ballCounts{2000, 20000, 200000, 1000000};
    int ticks = 240;
    bool fullSimulation = false;
    bool roofline = false;
    bool rooflineOnLevel = false;
    int substeps = 2;
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
    for (BackendKind kind : allBackendKinds) {
        std::string name(backendKindName(kind));
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (text == "all" || text.find(name) != std::string::npos) {
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
        } else if (argument == "--roofline") {
            options.roofline = true;
        } else if (argument == "--on-level") {
            options.rooflineOnLevel = true;
        } else if (argument == "--substeps") {
            options.substeps = std::atoi(value);
            ++i;
        } else if (argument == "--full") {
            options.fullSimulation = true;
        }
    }
    return options;
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

void runRawStep(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks) {
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    PhysicsBackend& backend = simulation.backend();
    const auto balls = scatterBalls(simulation.board(), ballCount, simulation.tuning().ballSpeed);
    backend.addBalls(balls);
    const StepParams params = simulation.stepParams();
    std::vector<BallHit> hits;
    for (int warmup = 0; warmup < 10; ++warmup) {
        hits.clear();
        backend.step(params, hits);
    }
    size_t hitTotal = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        hits.clear();
        backend.step(params, hits);
        hitTotal += hits.size();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double msPerTick = seconds * 1000.0 / ticks;
    const double ballStepsPerSecond = static_cast<double>(ballCount) * ticks / seconds;
    std::printf("%-7s balls=%9zu  %8.3f ms/tick  %8.1f M ball-steps/s  hits/tick=%zu\n",
                std::string(backendKindName(kind)).c_str(), ballCount, msPerTick, ballStepsPerSecond / 1e6,
                hitTotal / static_cast<size_t>(ticks));
}

void runFullSimulation(const BrickLayout& layout, BackendKind kind, size_t ballCount, int ticks) {
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    const size_t perColor = ballCount / ballColorCount;
    for (BallColor color : allBallColors) {
        simulation.launchBalls(color, static_cast<int>(perColor));
    }
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        simulation.tick();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-7s full balls=%9zu  %8.3f ms/tick  remaining balls=%zu bricks=%d\n",
                std::string(backendKindName(kind)).c_str(), ballCount, seconds * 1000.0 / ticks,
                simulation.backend().ballCount(), simulation.board().aliveBrickCount());
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
        const double achievedGigabytesPerSecond = static_cast<double>(ballCount * bytesPerBall) / (kernelMs * 1.0e6);
        std::printf("balls=%10zu substeps=%d kernel %7.3f ms  %6.1f GB/s = %5.1f%% of copy  %6.0f M ball-substeps/s\n",
                    ballCount, options.substeps, kernelMs, achievedGigabytesPerSecond,
                    100.0 * achievedGigabytesPerSecond / roofline.copyGigabytesPerSecond,
                    static_cast<double>(ballCount) * options.substeps / (kernelMs * 1.0e3));
    }
}

}

int main(int argumentCount, char** arguments) {
    const BenchOptions options = parseOptions(argumentCount, arguments);
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
    for (BackendKind kind : options.backends) {
        if (!isBackendAvailable(kind)) {
            std::printf("%-7s unavailable: %s\n", std::string(backendKindName(kind)).c_str(),
                        backendUnavailableReason(kind).c_str());
            continue;
        }
        for (size_t count : options.ballCounts) {
            if (options.fullSimulation) {
                runFullSimulation(level->bricks, kind, count, options.ticks);
            } else {
                runRawStep(level->bricks, kind, count, options.ticks);
            }
        }
    }
    return 0;
}
