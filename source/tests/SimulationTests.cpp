#include "TestFramework.h"

#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <random>
#include <tuple>
#include <vector>

namespace {

BrickLayout makeLayout(const char* text) {
    auto layout = parseBrickLayout(text);
    if (!layout) {
        throw TestFailure{layout.error()};
    }
    return *layout;
}

BrickLayout emptyLayout(int width, int rows) {
    BrickLayout layout;
    layout.width = width;
    layout.height = 1;
    layout.playableHeight = rows;
    layout.cellIds.assign(static_cast<size_t>(width), emptyBrickId);
    return layout;
}

const char* singleBrickLevel =
    "[Board]\nWidth 3\nHeight 1\nPlayableHeight 6\n[Layout]\n-1 0 -1\n[BrickTypes]\n0 Red 2\n";

StepParams paramsFor(const Simulation& simulation) {
    return simulation.stepParams();
}

} // namespace

TEST_CASE("ball reflects off the left wall with mirrored velocity") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.backend().addBalls(
        std::vector<BallSpawn>{{0.25f, 4.0f, -10.0f, 3.0f, toIndex(BallColor::Blue)}});
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
        simulation.backend().addBalls(
            std::vector<BallSpawn>{{1.5f, 1.5f, 0.0f, -14.0f, toIndex(BallColor::Red)}});
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
    simulation.backend().addBalls(
        std::vector<BallSpawn>{{1.5f, 1.5f, 0.0f, -14.0f, toIndex(BallColor::Green)}});
    for (int tick = 0; tick < 10; ++tick) {
        simulation.tick();
    }
    EXPECT(simulation.backend().ballCount() == 1);
    EXPECT(simulation.backend().exportBalls()[0].vy > 0.0f);
    EXPECT(simulation.board().aliveBrickCount() == 1);
}

TEST_CASE("bricks below a destroyed brick rise in their column") {
    BrickBoard board;
    board.load(makeLayout("[Board]\nWidth 1\nHeight 3\nPlayableHeight 5\n[Layout]\n0\n1\n2\n[BrickTypes]\n0 "
                          "Red 1\n1 Blue 1\n2 Green 1\n"));
    board.applyHit(0);
    EXPECT(board.settleColumns());
    EXPECT(board.brickAt(0, 0) == 1);
    EXPECT(board.brickAt(0, 1) == 2);
    EXPECT(board.brickAt(0, 2) == -1);
}

TEST_CASE("multi-cell brick rises only when every column above is free") {
    BrickBoard board;
    board.load(makeLayout("[Board]\nWidth 2\nHeight 2\nPlayableHeight 4\n[Layout]\n0 1\n2 2\n[BrickTypes]\n0 "
                          "Red 1\n1 Blue 1\n2 Green 1\n"));
    board.applyHit(0);
    board.settleColumns();
    EXPECT(board.brickAt(0, 1) == 2);
    board.applyHit(1);
    board.settleColumns();
    EXPECT(board.brickAt(0, 0) == 2);
    EXPECT(board.brickAt(1, 0) == 2);
}

TEST_CASE("head-on balls bounce apart only when ball collisions are enabled") {
    Simulation bouncing(createBackend(BackendKind::Scalar));
    Simulation passing(createBackend(BackendKind::Scalar));
    bouncing.load(makeLayout(singleBrickLevel));
    passing.load(makeLayout(singleBrickLevel));
    bouncing.setBallCollisions(true);
    passing.setBallCollisions(false);
    const std::vector<BallSpawn> spawns{{1.25f, 3.0f, 14.0f, 0.0f, toIndex(BallColor::Blue)},
                                        {1.75f, 3.0f, -14.0f, 0.0f, toIndex(BallColor::Blue)}};
    bouncing.backend().addBalls(spawns);
    passing.backend().addBalls(spawns);
    for (int tick = 0; tick < 4; ++tick) {
        bouncing.tick();
        passing.tick();
    }
    const auto bounced = bouncing.backend().exportBalls();
    const auto passed = passing.backend().exportBalls();
    EXPECT(bounced[0].vx < 0.0f);
    EXPECT(bounced[1].vx > 0.0f);
    EXPECT(bounced[0].x < bounced[1].x);
    EXPECT(passed[0].vx > 0.0f);
    EXPECT(passed[1].vx < 0.0f);
    EXPECT(passed[0].x > passed[1].x);
}

TEST_CASE("ball collisions restore the configured speed") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.setBallCollisions(true);
    const float expectedSpeed = simulation.tuning().ballSpeed;
    const float diagonalSpeed = std::sqrt(expectedSpeed * expectedSpeed - 100.0f);
    simulation.backend().addBalls(
        std::vector<BallSpawn>{{1.25f, 3.0f, 10.0f, diagonalSpeed, toIndex(BallColor::Blue)},
                               {1.75f, 3.0f, -14.0f, 0.0f, toIndex(BallColor::Blue)}});
    simulation.tick();
    const auto balls = simulation.backend().exportBalls();
    EXPECT(std::fabs(std::hypot(balls[0].vx, balls[0].vy) - expectedSpeed) <= 1e-3f);
    EXPECT(std::fabs(std::hypot(balls[1].vx, balls[1].vy) - expectedSpeed) <= 1e-3f);
}

TEST_CASE("only the scalar backend advertises ball collisions") {
    Simulation scalar(createBackend(BackendKind::Scalar));
    EXPECT(scalar.backend().supportsBallCollisions());
    if (isBackendAvailable(BackendKind::Avx2)) {
        Simulation avx2(createBackend(BackendKind::Avx2));
        EXPECT(!avx2.backend().supportsBallCollisions());
    }
}

TEST_CASE("dense random balls end a run without meaningful overlap") {
    constexpr int ballCount = 2000;
    constexpr int ticks = 120;
    constexpr float allowedOverlap = 0.01f;
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(emptyLayout(30, 38));
    simulation.setBallCollisions(true);
    simulation.scatterBalls(BallColor::Blue, ballCount);
    for (int tick = 0; tick < ticks; ++tick) {
        simulation.tick();
    }
    const auto balls = simulation.backend().exportBalls();
    EXPECT(balls.size() == static_cast<size_t>(ballCount));
    const float allowedDistance = 2.0f * simulation.tuning().ballRadius * (1.0f - allowedOverlap);
    const float allowedSquared = allowedDistance * allowedDistance;
    for (size_t first = 0; first < balls.size(); ++first) {
        for (size_t second = first + 1; second < balls.size(); ++second) {
            const float offsetX = balls[second].x - balls[first].x;
            const float offsetY = balls[second].y - balls[first].y;
            EXPECT(offsetX * offsetX + offsetY * offsetY >= allowedSquared);
        }
    }
}

TEST_CASE("disabling ball collisions matches never enabling them") {
    Simulation toggled(createBackend(BackendKind::Scalar));
    Simulation untouched(createBackend(BackendKind::Scalar));
    toggled.load(emptyLayout(20, 30));
    untouched.load(emptyLayout(20, 30));
    toggled.setBallCollisions(false);
    toggled.scatterBalls(BallColor::Green, 400);
    untouched.scatterBalls(BallColor::Green, 400);
    for (int tick = 0; tick < 60; ++tick) {
        toggled.tick();
        untouched.tick();
    }
    const auto toggledBalls = toggled.backend().exportBalls();
    const auto untouchedBalls = untouched.backend().exportBalls();
    EXPECT(toggledBalls.size() == untouchedBalls.size());
    for (size_t i = 0; i < toggledBalls.size(); ++i) {
        EXPECT(toggledBalls[i].x == untouchedBalls[i].x);
        EXPECT(toggledBalls[i].y == untouchedBalls[i].y);
        EXPECT(toggledBalls[i].vx == untouchedBalls[i].vx);
        EXPECT(toggledBalls[i].vy == untouchedBalls[i].vy);
        EXPECT(toggledBalls[i].color == untouchedBalls[i].color);
    }
}

TEST_CASE("every available backend matches the scalar reference") {
    auto level = loadLevel(
        levelFilesFor(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "performance", "DA_1000Bricks"));
    EXPECT(level.hasValue());
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
        const auto bricksDifference =
            std::abs(reference.board().aliveBrickCount() - candidate.board().aliveBrickCount());
        const auto ballsDifference =
            std::abs(static_cast<long long>(referenceBalls) - static_cast<long long>(candidateBalls));
        EXPECT(static_cast<size_t>(bricksDifference) <= reference.board().bricks().size() / 20);
        EXPECT(ballsDifference <= static_cast<long long>(referenceBalls / 20 + 10));
        for (size_t color = 0; color < ballColorCount; ++color) {
            const BallCount referenceColor = reference.stats().ballsByColor[color];
            const BallCount candidateColor = candidate.stats().ballsByColor[color];
            const BallCount colorDifference = referenceColor > candidateColor ? referenceColor - candidateColor
                                                                              : candidateColor - referenceColor;
            EXPECT(colorDifference <= referenceColor / 20 + 10);
        }
    }
}

TEST_CASE("Vulkan rejects an unknown compute adapter preference") {
    const std::string originalPreference = computeGpuPreference();
    if (!isBackendAvailable(BackendKind::Vulkan)) {
        return;
    }
    setComputeGpuPreference("999");
    const std::string reason = backendUnavailableReason(BackendKind::Vulkan);
    const BackendHandle backend = createBackend(BackendKind::Vulkan);
    setComputeGpuPreference(originalPreference);
    EXPECT(!backend);
    EXPECT(reason.find("adapter") != std::string::npos);
}

namespace {

struct ColumnOfBricks {
    static constexpr int columns = 3;
    static constexpr int rows = 6;
    static constexpr int paddedWidth = columns + 2;
    std::vector<int> cells = std::vector<int>(static_cast<size_t>(paddedWidth * (rows + 2)), -1);

    void place(int column, int row, int brick) { cells[static_cast<size_t>(paddedCellIndex(column, row, paddedWidth))] = brick; }
    int occupant(int index, int& color) const {
        color = cells[static_cast<size_t>(index)];
        return cells[static_cast<size_t>(index)];
    }
};

}

TEST_CASE("a ball only retargets a brick it can see, so blocked targets cannot trap it in a loop") {
    ColumnOfBricks board;
    board.place(1, 0, 0);
    board.place(1, 2, 1);
    EXPECT(!targetInSight(1.5f, 5.5f, 1, 0, board, ColumnOfBricks::paddedWidth));
    EXPECT(targetInSight(1.5f, 1.5f, 1, 0, board, ColumnOfBricks::paddedWidth));
    EXPECT(targetInSight(0.3f, 1.2f, 1, 0, board, ColumnOfBricks::paddedWidth));
    EXPECT(!targetInSight(1.4f, 4.0f, 1, 0, board, ColumnOfBricks::paddedWidth));
}

namespace {

std::vector<BallSpawn> scatteredBalls(size_t count) {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(0.3f, 2.7f);
    std::uniform_real_distribution<float> speed(-10.0f, 10.0f);
    std::vector<BallSpawn> balls(count);
    for (size_t i = 0; i < count; ++i) {
        balls[i] = {unit(random), unit(random) + 2.0f, speed(random), speed(random), static_cast<int32_t>(i % 7)};
    }
    return balls;
}

std::vector<std::tuple<float, float, float, float, int32_t>> sortedState(const std::vector<BallSpawn>& balls) {
    std::vector<std::tuple<float, float, float, float, int32_t>> state;
    for (const BallSpawn& ball : balls) {
        state.emplace_back(ball.x, ball.y, ball.vx, ball.vy, ball.color);
    }
    std::ranges::sort(state);
    return state;
}

// A backend that holds only a few balls, to stand in for one whose memory is too small for the balls in play. With no
// declared limit it takes the first chunks and silently drops the rest, as a backend that faults partway does.
class SmallBackend final : public PhysicsBackend {
public:
    std::string_view name() const override { return "Small"; }
    void uploadBoard(const BoardUpload&) override {}
    explicit SmallBackend(bool declaresLimit) : declaresLimit(declaresLimit) {}
    void addBalls(std::span<const BallSpawn> spawns) override {
        if (balls.size() + spawns.size() <= heldBalls) {
            balls.insert(balls.end(), spawns.begin(), spawns.end());
        }
    }
    void step(const StepParams&, std::vector<BallHit>&) override {}
    void removeBalls(std::span<const uint32_t>) override {}
    size_t ballCount() const override { return balls.size(); }
    size_t ballCapacityLimit() const override { return declaresLimit ? heldBalls : std::numeric_limits<size_t>::max(); }
    BallSnapshot snapshot() override { return {}; }
    std::vector<BallSpawn> exportBalls() override { return balls; }
    std::vector<BallSpawn> takeTail(size_t) override { return {}; }
    void clear() override { balls.clear(); }
    BallTally truncate(size_t) override { return {}; }

private:
    static constexpr size_t heldBalls = 300;
    bool declaresLimit;
    std::vector<BallSpawn> balls;
};

} // namespace

TEST_CASE("switching backends streams every ball across with its position and velocity") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    const std::vector<BallSpawn> balls = scatteredBalls(10'000);
    simulation.backend().addBalls(balls);
    const BackendReplacement replacement = simulation.replaceBackend(createBackend(BackendKind::Scalar), 777);
    EXPECT(replacement.ballsToRefill == 0);
    EXPECT(replacement.retired->ballCount() == 0);
    EXPECT(simulation.backend().ballCount() == balls.size());
    EXPECT(sortedState(simulation.backend().exportBalls()) == sortedState(balls));
}

TEST_CASE("balls a new backend cannot hold are handed back to be launched again") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.backend().addBalls(scatteredBalls(1'000));
    const BackendReplacement replacement = simulation.replaceBackend(
        BackendHandle(new SmallBackend(true), BackendRelease{[](PhysicsBackend* backend) { delete backend; }}));
    EXPECT(replacement.ballsToRefill == 1'000);
    EXPECT(simulation.backend().ballCount() == 0);
}

TEST_CASE("balls a new backend drops partway are counted and handed back") {
    Simulation simulation(createBackend(BackendKind::Scalar));
    simulation.load(makeLayout(singleBrickLevel));
    simulation.backend().addBalls(scatteredBalls(1'000));
    const BackendReplacement replacement = simulation.replaceBackend(
        BackendHandle(new SmallBackend(false), BackendRelease{[](PhysicsBackend* backend) { delete backend; }}), 100);
    EXPECT(simulation.backend().ballCount() == 300);
    EXPECT(replacement.ballsToRefill == 700);
}
