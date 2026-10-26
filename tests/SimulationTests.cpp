#include "TestFramework.h"

#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"

#include <cmath>
#include <filesystem>

namespace {

BrickLayout makeLayout(const char* text) {
    auto layout = parseBrickLayout(text);
    if (!layout) {
        throw TestFailure{layout.error()};
    }
    return *layout;
}

const char* singleBrickLevel = "[Board]\nWidth 3\nHeight 1\nPlayableHeight 6\n[Layout]\n-1 0 -1\n[BrickTypes]\n0 Red 2\n";

StepParams paramsFor(const Simulation& simulation) {
    const SimulationTuning& tuning = simulation.tuning();
    const BrickBoard& board = simulation.board();
    return {tuning.tickSeconds, tuning.ballRadius, tuning.ballSpeed, static_cast<float>(board.columns()),
            static_cast<float>(board.rows()), board.paddedWidth(), board.paddedCellCount(),
            tuning.retargetBounceThreshold};
}

}

TEST_CASE("ball reflects off the left wall with mirrored velocity") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.backend().addBalls(std::vector<BallSpawn>{{0.25f, 4.0f, -10.0f, 3.0f, toIndex(BallColor::Blue)}});
    std::vector<BallHit> hits;
    simulation.backend().step(paramsFor(simulation), hits);
    const auto balls = simulation.backend().exportBalls();
    EXPECT(balls[0].vx == 10.0f);
    EXPECT(balls[0].vy == 3.0f);
    EXPECT(balls[0].x >= simulation.tuning().ballRadius);
}

TEST_CASE("multi-hit brick breaks after exact hit count") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    for (int ball = 0; ball < 2; ++ball) {
        EXPECT(simulation.board().aliveBrickCount() == 1);
        simulation.backend().addBalls(std::vector<BallSpawn>{{1.5f, 1.5f, 0.0f, -14.0f, toIndex(BallColor::Red)}});
        for (int tick = 0; tick < 20 && simulation.backend().ballCount() > 0; ++tick) {
            simulation.tick();
        }
        EXPECT(simulation.backend().ballCount() == 0);
    }
    EXPECT(simulation.board().aliveBrickCount() == 0);
    EXPECT(simulation.isCleared());
}

TEST_CASE("wrong color ball bounces off brick and survives") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.backend().addBalls(std::vector<BallSpawn>{{1.5f, 1.5f, 0.0f, -14.0f, toIndex(BallColor::Green)}});
    for (int tick = 0; tick < 10; ++tick) {
        simulation.tick();
    }
    EXPECT(simulation.backend().ballCount() == 1);
    EXPECT(simulation.backend().exportBalls()[0].vy > 0.0f);
    EXPECT(simulation.board().aliveBrickCount() == 1);
}

TEST_CASE("bricks below a destroyed brick rise in their column") {
    BrickBoard board;
    board.load(makeLayout("[Board]\nWidth 1\nHeight 3\nPlayableHeight 5\n[Layout]\n0\n1\n2\n[BrickTypes]\n0 Red 1\n1 Blue 1\n2 Green 1\n"));
    board.applyHit(0);
    EXPECT(board.settleColumns());
    EXPECT(board.brickAt(0, 0) == 1);
    EXPECT(board.brickAt(0, 1) == 2);
    EXPECT(board.brickAt(0, 2) == -1);
}

TEST_CASE("multi-cell brick rises only when every column above is free") {
    BrickBoard board;
    board.load(makeLayout(
        "[Board]\nWidth 2\nHeight 2\nPlayableHeight 4\n[Layout]\n0 1\n2 2\n[BrickTypes]\n0 Red 1\n1 Blue 1\n2 Green 1\n"));
    board.applyHit(0);
    board.settleColumns();
    EXPECT(board.brickAt(0, 1) == 2);
    board.applyHit(1);
    board.settleColumns();
    EXPECT(board.brickAt(0, 0) == 2);
    EXPECT(board.brickAt(1, 0) == 2);
}

TEST_CASE("every available backend matches the scalar reference") {
    auto level = loadLevel(levelFilesFor(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "performance", "DA_1000Bricks"));
    EXPECT(level.has_value());
    for (BackendKind kind : allBackendKinds) {
        if (kind == BackendKind::Scalar || !isBackendAvailable(kind)) {
            continue;
        }
        Simulation reference(createBackend(BackendKind::Scalar));
        Simulation candidate(createBackend(kind));
        reference.load(level->bricks);
        candidate.load(level->bricks);
        for (BallColor color : allBallColors) {
            reference.launchBalls(color, 700);
            candidate.launchBalls(color, 700);
        }
        for (int tick = 0; tick < 240; ++tick) {
            reference.tick();
            candidate.tick();
        }
        const auto referenceBalls = reference.backend().ballCount();
        const auto candidateBalls = candidate.backend().ballCount();
        const auto bricksDifference = std::abs(reference.board().aliveBrickCount() - candidate.board().aliveBrickCount());
        const auto ballsDifference = std::abs(static_cast<long long>(referenceBalls) - static_cast<long long>(candidateBalls));
        EXPECT(bricksDifference <= reference.board().bricks().size() / 20);
        EXPECT(ballsDifference <= static_cast<long long>(referenceBalls / 20 + 10));
    }
}
