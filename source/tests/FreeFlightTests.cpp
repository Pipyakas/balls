#include "TestFramework.h"

#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/PhysicsAccuracy.h"
#include "sim/Simulation.h"
#include "sim/kernels/BallStepCore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int fieldColumns = 30;
constexpr int fieldRows = 38;
constexpr float positionTolerance = 0.02f;

StepParams emptyFieldParams() {
    StepParams params{};
    params.deltaTime = 1.0f / 120.0f;
    params.radius = 0.2f;
    params.speed = 14.0f;
    params.fieldWidth = static_cast<float>(fieldColumns);
    params.fieldHeight = static_cast<float>(fieldRows);
    params.paddedWidth = fieldColumns + 2;
    params.paddedCellCount = (fieldColumns + 2) * (fieldRows + 2);
    params.retargetBounceThreshold = 3;
    params.substeps = 1;
    return params;
}

struct EmptyBoard {
    std::vector<int> paddedCells;
    std::vector<int> brickColors{0};
    std::vector<int> targetCells;
    std::vector<int> nearBrickCells;

    explicit EmptyBoard(const StepParams& params)
        : paddedCells(static_cast<size_t>(params.paddedCellCount), -1),
          targetCells(static_cast<size_t>(params.paddedCellCount) * 7, -1),
          nearBrickCells(static_cast<size_t>(params.paddedCellCount), 0) {}

    BoardView view() const { return {paddedCells.data(), brickColors.data(), targetCells.data(), nearBrickCells.data()}; }
};

}

TEST_CASE("free flight lands where stepping through an empty box does") {
    const StepParams params = emptyFieldParams();
    const EmptyBoard board(params);
    const float angle = 0.7f;
    float x = 3.3f;
    float y = 11.9f;
    float vx = std::cos(angle) * params.speed;
    float vy = -std::sin(angle) * params.speed;
    const float startX = x;
    const float startY = y;
    const float startVx = vx;
    const float startVy = vy;
    int bounces = 0;
    constexpr unsigned int ticks = 600;
    for (unsigned int tick = 0; tick < ticks; ++tick) {
        stepBall(x, y, vx, vy, 0, bounces, board.view(), params);
    }
    const FreeFlightPosition flown = flyFreely(startX, startY, startVx, startVy, ticks, params);
    EXPECT(std::fabs(flown.x - x) < positionTolerance);
    EXPECT(std::fabs(flown.y - y) < positionTolerance);
    EXPECT((flown.vx > 0.0f) == (vx > 0.0f));
    EXPECT((flown.vy > 0.0f) == (vy > 0.0f));
    EXPECT(flown.wallCrossings == static_cast<unsigned int>(bounces));
}

TEST_CASE("flying backwards then forwards returns to the same ball") {
    const StepParams params = emptyFieldParams();
    constexpr unsigned int ticks = 123457;
    const FreeFlightPosition epoch = flyFreelyBackwards(17.25f, 4.5f, -9.0f, 10.7f, ticks, params);
    const FreeFlightPosition now = flyFreely(epoch.x, epoch.y, epoch.vx, epoch.vy, ticks, params);
    EXPECT(std::fabs(now.x - 17.25f) < positionTolerance);
    EXPECT(std::fabs(now.y - 4.5f) < positionTolerance);
    EXPECT(now.vx < 0.0f);
    EXPECT(now.vy > 0.0f);
}

TEST_CASE("free flight stays inside the walls over a long flight") {
    const StepParams params = emptyFieldParams();
    for (unsigned int ticks = 1; ticks < 5'000'000u; ticks = ticks * 3 + 7) {
        const FreeFlightPosition flown = flyFreely(0.2f, 37.8f, 13.9f, -1.6f, ticks, params);
        EXPECT(flown.x >= params.radius - 1e-4f && flown.x <= params.fieldWidth - params.radius + 1e-4f);
        EXPECT(flown.y >= params.radius - 1e-4f && flown.y <= params.fieldHeight - params.radius + 1e-4f);
    }
}

namespace {

std::vector<BallSpawn> flyOnEmptyBoard(BackendKind kind) {
    BrickLayout layout;
    layout.width = fieldColumns;
    layout.height = 1;
    layout.playableHeight = fieldRows;
    layout.cellIds.assign(static_cast<size_t>(fieldColumns), emptyBrickId);
    Simulation simulation(createBackend(kind));
    simulation.load(layout);
    // Fewer slots than balls, as in a Performance game: backends only freeze while the grid samples a frame.
    constexpr int visibilitySlotsPerSide = 16;
    simulation.backend().setVisibilityGrid(visibilitySlotsPerSide, visibilitySlotsPerSide);
    simulation.scatterBalls(BallColor::Red, 3000);
    for (int frame = 0; frame < 90; ++frame) {
        simulation.tick(2);
    }
    simulation.scatterBalls(BallColor::Blue, 3000);
    for (int frame = 0; frame < 90; ++frame) {
        simulation.tick(2);
    }
    return simulation.backend().exportBalls();
}

}

TEST_CASE("balls flying freely on CUDA and AVX2 match Scalar stepping, including balls added mid-flight") {
    const std::vector<BallSpawn> stepped = flyOnEmptyBoard(BackendKind::Scalar);
    for (const BackendKind kind : {BackendKind::Cuda, BackendKind::Avx2}) {
        if (!isBackendAvailable(kind)) {
            continue;
        }
        const std::vector<BallSpawn> flown = flyOnEmptyBoard(kind);
        EXPECT(stepped.size() == flown.size());
        constexpr float driftTolerance = 0.05f;
        float worst = 0.0f;
        for (size_t i = 0; i < std::min(stepped.size(), flown.size()); ++i) {
            worst = std::max({worst, std::fabs(stepped[i].x - flown[i].x), std::fabs(stepped[i].y - flown[i].y)});
        }
        EXPECT(worst < driftTolerance);
    }
}

TEST_CASE("every optimized backend stays within accuracy bounds of the naive reference engine") {
    constexpr size_t boxBalls = 3000;
    constexpr int boxFrames = 300;
    constexpr double meanPositionBound = 0.02;
    constexpr double maxPositionBound = 0.06;
    BrickLayout box;
    box.width = fieldColumns;
    box.height = 1;
    box.playableHeight = fieldRows;
    box.cellIds.assign(static_cast<size_t>(fieldColumns), emptyBrickId);
    for (const bool grid : {true, false}) {
        AccuracyScenario scenario{"empty box", box, boxBalls, false, boxFrames};
        scenario.visibilityGrid = grid;
        const AccuracyRun reference = runAccuracyScenario(createReferenceBackend(), scenario);
        for (const BackendKind kind : allBackendKinds) {
            if (!isBackendAvailable(kind)) {
                continue;
            }
            const AccuracyReport report = compareToReference(runAccuracyScenario(createBackend(kind), scenario), reference);
            EXPECT(report.comparedPositions);
            EXPECT(report.meanPositionError < meanPositionBound);
            EXPECT(report.maxPositionError < maxPositionBound);
            EXPECT(report.worstOutsideField == 0);
            EXPECT(report.worstSpeedError < 1e-3);
        }
    }
}
