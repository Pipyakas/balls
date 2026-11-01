#include "sim/HybridBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>

namespace {

constexpr double initialCpuShare = 0.08;
constexpr double maximumCpuShare = 0.5;
constexpr double shareSmoothing = 0.1;
constexpr double settlingSmoothing = 0.35;
constexpr int settlingMeasurements = 24;
constexpr size_t minimumBallsToMeasure = 65536;
constexpr size_t minimumHybridBalls = 262144;
constexpr size_t minimumRebalanceBalls = 131072;
constexpr double rebalanceTolerance = 0.02;

constexpr size_t maximumBallsMovedPerStep = 1u << 20;
constexpr double slotSelectionRange = 4294967296.0;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

uint32_t slotHash(uint32_t slot) {
    uint32_t hash = slot * 0x9e3779b9u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return hash;
}

void destroyHybrid(PhysicsBackend* backend) { delete backend; }

}

HybridBackend::HybridBackend(BackendHandle gpuBackend, BackendHandle cpuBackend)
    : gpu(std::move(gpuBackend)), cpu(std::move(cpuBackend)), measuredCpuShare(initialCpuShare) {
    cpu->shareCoresWithGpu();
}

BackendHandle HybridBackend::create(BackendHandle gpu, BackendHandle cpu) {
    if (!gpu || !cpu) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    return BackendHandle(new HybridBackend(std::move(gpu), std::move(cpu)), BackendRelease{destroyHybrid, nullptr});
}

void HybridBackend::uploadBoard(const BoardUpload& board) {
    gpu->uploadBoard(board);
    cpu->uploadBoard(board);
}

size_t HybridBackend::targetCpuCount(size_t total) const {
    if (total < minimumHybridBalls) {
        return 0;
    }
    return static_cast<size_t>(std::llround(static_cast<double>(total) * measuredCpuShare));
}

size_t HybridBackend::ballCapacityLimit() const {
    const size_t gpuLimit = gpu->ballCapacityLimit();
    const size_t cpuLimit = cpu->ballCapacityLimit();
    return gpuLimit > std::numeric_limits<size_t>::max() - cpuLimit ? std::numeric_limits<size_t>::max()
                                                                     : gpuLimit + cpuLimit;
}

size_t HybridBackend::ballHardLimit() const {
    const size_t gpuLimit = gpu->ballHardLimit();
    const size_t cpuLimit = cpu->ballHardLimit();
    return gpuLimit > std::numeric_limits<size_t>::max() - cpuLimit ? std::numeric_limits<size_t>::max()
                                                                     : gpuLimit + cpuLimit;
}

void HybridBackend::addBalls(std::span<const BallSpawn> spawns) {
    const size_t cpuCount = cpu->ballCount();
    const size_t target = targetCpuCount(ballCount() + spawns.size());
    const size_t gpuLimit = gpu->ballCapacityLimit();
    const size_t gpuRoom = gpuLimit > gpu->ballCount() ? gpuLimit - gpu->ballCount() : 0;
    const size_t cpuShare = std::min(target > cpuCount ? target - cpuCount : 0, spawns.size());
    const size_t gpuTake = std::min(spawns.size() - cpuShare, gpuRoom);
    gpu->addBalls(spawns.first(gpuTake));
    cpu->addBalls(spawns.last(spawns.size() - gpuTake));
}

void HybridBackend::rebalance() {
    const size_t total = ballCount();
    const size_t current = cpu->ballCount();
    const size_t target = targetCpuCount(total);
    const size_t difference = current > target ? current - target : target - current;
    const size_t tolerance =
        std::max(minimumRebalanceBalls, static_cast<size_t>(static_cast<double>(target) * rebalanceTolerance));
    if (difference <= tolerance && !(target == 0 && current > 0)) {
        return;
    }
    const size_t moved = std::min(difference, maximumBallsMovedPerStep);
    PhysicsBackend& source = current < target ? *gpu : *cpu;
    PhysicsBackend& destination = current < target ? *cpu : *gpu;
    if (source.takeCompactTail(moved, movedBalls)) {
        if (!destination.addCompact(movedBalls)) {
            source.addCompact(movedBalls);
        }
        return;
    }
    const std::vector<BallSpawn> balls = source.takeTail(moved);
    destination.addBalls(balls);
}

void HybridBackend::updateShare(double cpuMs, size_t cpuCount, double gpuMs, size_t gpuCount) {
    if (cpuCount < minimumBallsToMeasure || gpuCount < minimumBallsToMeasure || cpuMs <= 0.0 || gpuMs <= 0.0) {
        return;
    }
    const double cpuCostPerBall = cpuMs / static_cast<double>(cpuCount);
    const double gpuCostPerBall = gpuMs / static_cast<double>(gpuCount);
    const double balancedShare = gpuCostPerBall / (cpuCostPerBall + gpuCostPerBall);
    const double smoothing = shareMeasurements < settlingMeasurements ? settlingSmoothing : shareSmoothing;
    ++shareMeasurements;
    measuredCpuShare += smoothing * (std::min(balancedShare, maximumCpuShare) - measuredCpuShare);
}

void HybridBackend::step(const StepParams& params, std::vector<BallHit>& hits) {

    if (!params.freeFlight) {
        rebalance();
    }
    stepGridWidth = gridWidth;
    stepGridHeight = gridHeight;
    const size_t cpuCount = cpu->ballCount();
    const size_t gpuCount = gpu->ballCount();
    cpuHits.clear();
    unusedHits.clear();
    std::future<double> cpuStep;
    if (cpuCount > 0) {
        cpuStep = std::async(std::launch::async, [this, &params] {
            const Clock::time_point start = Clock::now();
            cpu->step(params, cpuHits);
            return millisecondsSince(start);
        });
    }
    const Clock::time_point gpuStart = Clock::now();
    gpu->step(params, unusedHits);
    lastGpuMs = millisecondsSince(gpuStart);
    lastCpuMs = cpuStep.valid() ? cpuStep.get() : 0.0;
    if (!params.freeFlight) {
        updateShare(lastCpuMs, cpuCount, lastGpuMs, gpuCount);
    }
    const uint32_t offset = static_cast<uint32_t>(gpu->ballCount());
    hits.reserve(hits.size() + cpuHits.size());
    for (const BallHit& hit : cpuHits) {
        hits.push_back({hit.ball + offset, hit.brick});
    }
}

void HybridBackend::removeBalls(std::span<const uint32_t> descendingIndices) {
    const uint32_t offset = static_cast<uint32_t>(gpu->ballCount());
    std::vector<uint32_t> cpuIndices;
    cpuIndices.reserve(descendingIndices.size());
    for (uint32_t index : descendingIndices) {
        if (index >= offset) {
            cpuIndices.push_back(index - offset);
        }
    }
    cpu->removeBalls(cpuIndices);
}

void HybridBackend::overlay(const BallSnapshot& balls, uint32_t replaceBelow) {
    if (!balls.packed || balls.count == 0) {
        return;
    }
    const auto place = [&](size_t slot, uint32_t value) {
        if (merged[slot] == emptyRenderSlot || slotHash(static_cast<uint32_t>(slot)) < replaceBelow) {
            merged[slot] = value;
        }
    };
    if (balls.visibilityGrid) {
        if (balls.count != merged.size()) {
            return;
        }
        for (size_t slot = 0; slot < balls.count; ++slot) {
            if (balls.packed[slot] != emptyRenderSlot) {
                place(slot, balls.packed[slot]);
            }
        }
        return;
    }
    const float coordinateToColumn = static_cast<float>(stepGridWidth) / (renderCoordinateMax + 1);
    const float coordinateToRow = static_cast<float>(stepGridHeight) / (renderCoordinateMax + 1);
    for (size_t i = 0; i < balls.count; ++i) {
        const uint32_t value = balls.packed[i];
        const int column = std::min(static_cast<int>(static_cast<float>(value & renderCoordinateMax) * coordinateToColumn),
                                    stepGridWidth - 1);
        const int row = std::min(
            static_cast<int>(static_cast<float>((value >> renderCoordinateBits) & renderCoordinateMax) * coordinateToRow),
            stepGridHeight - 1);
        place(static_cast<size_t>(row) * static_cast<size_t>(stepGridWidth) + static_cast<size_t>(column), value);
    }
}

BallSnapshot HybridBackend::snapshot() {
    const BallSnapshot gpuBalls = gpu->snapshot();
    if (gpuBalls.residentInRenderBuffer || cpu->ballCount() == 0) {
        return gpuBalls;
    }
    const BallSnapshot cpuBalls = cpu->snapshot();
    const float fieldWidth = cpuBalls.fieldWidth;
    const float fieldHeight = cpuBalls.fieldHeight;
    if (!gpuBalls.visibilityGrid && !cpuBalls.visibilityGrid) {
        merged.clear();
        if (gpuBalls.packed) {
            merged.assign(gpuBalls.packed, gpuBalls.packed + gpuBalls.count);
        }
        if (cpuBalls.packed) {
            merged.insert(merged.end(), cpuBalls.packed, cpuBalls.packed + cpuBalls.count);
        }
        return {merged.data(), merged.size(), fieldWidth, fieldHeight, false, false};
    }
    merged.assign(gridCellCount(), emptyRenderSlot);
    if (merged.empty()) {
        return gpuBalls;
    }
    const double total = static_cast<double>(ballCount());
    const double cpuFraction = total > 0.0 ? static_cast<double>(cpu->ballCount()) / total : 0.0;
    overlay(gpuBalls, 0u);
    overlay(cpuBalls, static_cast<uint32_t>(std::min(cpuFraction * slotSelectionRange,
                                                     static_cast<double>(std::numeric_limits<uint32_t>::max()))));
    return {merged.data(), merged.size(), fieldWidth, fieldHeight, false, true};
}

std::vector<BallSpawn> HybridBackend::exportBalls() {
    std::vector<BallSpawn> balls = gpu->exportBalls();
    const std::vector<BallSpawn> cpuBalls = cpu->exportBalls();
    balls.insert(balls.end(), cpuBalls.begin(), cpuBalls.end());
    return balls;
}

std::vector<BallSpawn> HybridBackend::takeTail(size_t count) {
    std::vector<BallSpawn> balls = cpu->takeTail(count);
    if (balls.size() < count) {
        const std::vector<BallSpawn> gpuBalls = gpu->takeTail(count - balls.size());
        balls.insert(balls.end(), gpuBalls.begin(), gpuBalls.end());
    }
    return balls;
}

void HybridBackend::clear() {
    gpu->clear();
    cpu->clear();
}

BallTally HybridBackend::truncate(size_t newCount) {
    const size_t total = ballCount();
    if (newCount >= total) {
        return {};
    }
    const size_t cpuCount = cpu->ballCount();
    const size_t gpuCount = gpu->ballCount();
    size_t cpuKeep = static_cast<size_t>(static_cast<double>(newCount) * static_cast<double>(cpuCount) /
                                         static_cast<double>(total));
    cpuKeep = std::clamp(cpuKeep, newCount > gpuCount ? newCount - gpuCount : size_t{0}, std::min(cpuCount, newCount));
    BallTally removed = gpu->truncate(newCount - cpuKeep);
    const BallTally cpuRemoved = cpu->truncate(cpuKeep);
    for (size_t color = 0; color < removed.size(); ++color) {
        removed[color] += cpuRemoved[color];
    }
    return removed;
}

std::string_view HybridBackend::faultMessage() const {
    const std::string_view gpuFault = gpu->faultMessage();
    return gpuFault.empty() ? cpu->faultMessage() : gpuFault;
}

void HybridBackend::setVisibilityGrid(int width, int height) {
    gridWidth = std::max(width, 0);
    gridHeight = std::max(height, 0);
    gpu->setVisibilityGrid(width, height);
    cpu->setVisibilityGrid(width, height);
}
