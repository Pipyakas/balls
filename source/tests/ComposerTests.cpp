#include "TestFramework.h"

#include "level/LevelParser.h"
#include "sim/BackendFactory.h"
#include "sim/BackendSelection.h"
#include "sim/DeviceComposer.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <functional>
#include <numeric>

namespace {

class FakeDeviceBackend final : public PhysicsBackend {
public:
    FakeDeviceBackend() : inner(createBackend(BackendKind::Scalar)) {}

    std::string_view name() const override { return "FakeDevice"; }
    void uploadBoard(const BoardUpload& board) override {
        inner->uploadBoard(board);
        if (!board.brickHitsLeft.empty()) {
            hitsLeft.assign(board.brickHitsLeft.begin(), board.brickHitsLeft.end());
        }
    }
    void uploadBrickHits(std::span<const int32_t> hits) override { hitsLeft.assign(hits.begin(), hits.end()); }
    void addBalls(std::span<const BallSpawn> spawns) override { inner->addBalls(spawns); }
    void step(const StepParams& params, std::vector<BallHit>&) override {
        innerHits.clear();
        inner->step(params, innerHits);
        hitBricks.clear();
        dead.clear();
        tally = {};
        const std::vector<BallSpawn> balls = innerHits.empty() ? std::vector<BallSpawn>{} : inner->exportBalls();
        for (const BallHit& hit : innerHits) {
            int32_t& left = hitsLeft[static_cast<size_t>(hit.brick)];
            if (left > 0 && std::ranges::find(dead, hit.ball) == dead.end()) {
                --left;
                hitBricks.push_back(static_cast<uint32_t>(hit.brick));
                dead.push_back(hit.ball);
                ++tally[static_cast<size_t>(balls[hit.ball].color)];
            }
        }
        std::ranges::sort(dead, std::greater<>{});
        inner->removeBalls(dead);
    }
    void removeBalls(std::span<const uint32_t>) override {}
    size_t ballCount() const override { return inner->ballCount(); }
    BallSnapshot snapshot() override { return inner->snapshot(); }
    std::vector<BallSpawn> exportBalls() override { return inner->exportBalls(); }
    std::vector<BallSpawn> takeTail(size_t count) override { return inner->takeTail(count); }
    void clear() override { inner->clear(); }
    BallTally truncate(size_t newCount) override { return inner->truncate(newCount); }
    bool resolvesHitsOnDevice() const override { return true; }
    DeviceHitResults deviceHitResults() const override {
        return {{}, hitBricks, tally, static_cast<BallCount>(hitBricks.size())};
    }

private:
    BackendHandle inner;
    std::vector<int32_t> hitsLeft;
    std::vector<BallHit> innerHits;
    std::vector<uint32_t> hitBricks;
    std::vector<uint32_t> dead;
    BallTally tally{};
};

void destroyFake(PhysicsBackend* backend) { delete backend; }

BackendHandle fakeDevice() { return BackendHandle(new FakeDeviceBackend(), BackendRelease{destroyFake, nullptr}); }

constexpr DeviceComposerTuning smallTuning{1000, 500, 200};

BackendHandle composed(std::vector<std::function<BackendHandle()>> makers) {
    std::vector<DeviceComposer::PartSpec> parts;
    for (const auto& make : makers) {
        parts.push_back({make(), "part", 1.0});
    }
    return DeviceComposer::create(std::move(parts), smallTuning);
}

const char* crowdedLevel = "[Board]\nWidth 6\nHeight 1\nPlayableHeight 10\n[Layout]\n0 1 2 3 4 5\n"
                           "[BrickTypes]\n0 Red 40\n1 Red 40\n2 Red 40\n3 Red 40\n4 Red 40\n5 Red 40\n";

BrickLayout crowdedLayout() {
    auto layout = parseBrickLayout(crowdedLevel);
    EXPECT(layout.hasValue());
    return *layout;
}

int hitsLeftOnBoard(const Simulation& simulation) {
    const std::vector<int32_t> hits = simulation.board().hitsLeftPerBrick();
    return std::accumulate(hits.begin(), hits.end(), 0, [](int sum, int32_t left) { return sum + std::max(left, 0); });
}

void runAndCheckHitAccounting(BackendHandle backend, bool expectSplit) {
    Simulation simulation(std::move(backend));
    simulation.load(crowdedLayout());
    const int initialHits = hitsLeftOnBoard(simulation);
    simulation.launchBalls(BallColor::Red, 6000);
    auto& composer = static_cast<DeviceComposer&>(simulation.backend());
    bool split = false;
    for (int tick = 0; tick < 600 && !simulation.isCleared(); ++tick) {
        simulation.tick();
        if (composer.partBallCount(0) > 0 && composer.partBallCount(1) > 0) {
            split = true;
        }
        const BallCount died = simulation.stats().totalBallsSpawned - simulation.backend().ballCount();
        EXPECT(simulation.stats().totalHits == died);
        EXPECT(static_cast<BallCount>(initialHits - hitsLeftOnBoard(simulation)) == died);
    }
    EXPECT(split == expectSplit);
    EXPECT(simulation.isCleared());
}

}

TEST_CASE("two device-resolving parts never consume more brick hits than a brick has") {
    runAndCheckHitAccounting(composed({fakeDevice, fakeDevice}), true);
}

TEST_CASE("three device-resolving parts split brick budgets exactly") {
    runAndCheckHitAccounting(composed({fakeDevice, fakeDevice, fakeDevice}), true);
}

TEST_CASE("a device part and a host part share bricks like the CUDA+AVX2 pair") {
    runAndCheckHitAccounting(composed({fakeDevice, [] { return createBackend(BackendKind::Scalar); }}), true);
}

TEST_CASE("two host parts map hits back to the right balls") {
    const auto scalar = [] { return createBackend(BackendKind::Scalar); };
    runAndCheckHitAccounting(composed({scalar, scalar}), true);
}

TEST_CASE("composer keeps balls, snapshots and truncation consistent across parts") {
    const auto scalar = [] { return createBackend(BackendKind::Scalar); };
    BackendHandle backend = composed({scalar, fakeDevice, scalar});
    Simulation simulation(std::move(backend));
    simulation.load(crowdedLayout());
    simulation.scatterBalls(BallColor::Blue, 5000);
    for (int tick = 0; tick < 30; ++tick) {
        simulation.tick();
    }
    auto& composer = static_cast<DeviceComposer&>(simulation.backend());
    EXPECT(composer.partCount() == 3);
    size_t sum = 0;
    for (size_t part = 0; part < composer.partCount(); ++part) {
        EXPECT(composer.partBallCount(part) > 0);
        sum += composer.partBallCount(part);
    }
    EXPECT(sum == composer.ballCount());
    EXPECT(composer.snapshot().count == composer.ballCount());
    EXPECT(composer.exportBalls().size() == composer.ballCount());
    const BallTally removed = composer.truncate(1234);
    EXPECT(composer.ballCount() == 1234);
    EXPECT(removed[toIndex(BallColor::Blue)] == sum - 1234);
    EXPECT(composer.takeTail(100).size() == 100);
    EXPECT(composer.ballCount() == 1134);
}

TEST_CASE("multi-device picks round-trip and reject overlap") {
    const auto picks = parseBackendPicks("VulkanMulti@gpu0|gpu1");
    if (!picks) {
        return;
    }
    EXPECT(picks->size() == 1 && picks->front().kind == BackendKind::VulkanMulti);
    EXPECT(formatBackendPicks(*picks) == "VulkanMulti@gpu0|gpu1");
    EXPECT(kindForPicks(*picks) == BackendKind::VulkanMulti);

    const std::vector<BackendPick> overlap{{BackendKind::VulkanMulti, "gpu0|gpu1"}, {BackendKind::Vulkan, "gpu0"}};
    EXPECT(!backendPicksProblem(overlap).empty());

    EXPECT(!parseBackendPicks("VulkanMulti@gpu0"));
}

TEST_CASE("backend picks round-trip through their text form") {
    const auto picks = parseBackendPicks("avx2@cpu");
    if (!picks) {
        return;
    }
    EXPECT(picks->size() == 1 && picks->front().kind == BackendKind::Avx2 && picks->front().device == "cpu");
    EXPECT(formatBackendPicks(*picks) == "AVX2@cpu");
    EXPECT(kindForPicks(*picks) == BackendKind::Avx2);
    EXPECT(!parseBackendPicks("avx2@nowhere"));
    EXPECT(!parseBackendPicks("cuda+avx2"));
    const std::vector<BackendPick> twice{{BackendKind::Avx2, "cpu"}, {BackendKind::Scalar, "cpu"}};
    EXPECT(backendPicksProblem(twice).find("two backends") != std::string::npos);
    EXPECT(parseStoredBackend("AVX2@cpu")->kind == BackendKind::Avx2);
    EXPECT(parseStoredBackend("CUDA+AVX2")->kind == BackendKind::CudaAvx2);
    EXPECT(storedBackendName(BackendKind::Vulkan) == "Vulkan");
}
