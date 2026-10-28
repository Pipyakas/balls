#include "sim/DeviceComposer.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <numeric>

namespace {

// Moving balls costs a round trip through the host and leaves the receiving GPU's balls unsorted, so once the
// shares have settled a part has to be 10% off its target, and moves are spaced out.
constexpr double settlingRebalanceTolerance = 0.02;
constexpr double settledRebalanceTolerance = 0.1;
constexpr int settledMoveCooldownSteps = 30;
constexpr size_t maximumBallsMovedPerStep = 1u << 22;
constexpr double minimumShare = 0.01;
constexpr double shareSmoothing = 0.1;
constexpr double settlingSmoothing = 0.35;
constexpr int settlingMeasurements = 24;
constexpr double slotSelectionRange = 4294967296.0;
// A part is parked once the composed step has been this much slower than the lead part alone would be, for this
// many settled steps in a row: a CPU or small GPU whose fixed cost per step exceeds what its share saves.
constexpr double parkMargin = 0.03;
constexpr int parkAfterSteps = 60;
constexpr double wallSmoothing = 0.1;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

uint32_t mixHash(uint32_t value) {
    uint32_t hash = value * 0x9e3779b9u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

size_t saturatingAdd(size_t left, size_t right) {
    return left > std::numeric_limits<size_t>::max() - right ? std::numeric_limits<size_t>::max() : left + right;
}

void destroyComposer(PhysicsBackend* backend) { delete backend; }

}

DeviceComposer::DeviceComposer(std::vector<PartSpec> specs, Tuning tuning) : settings(tuning) {
    std::ranges::stable_partition(specs, [](const PartSpec& spec) { return spec.backend->resolvesHitsOnDevice(); });
    double totalWeight = 0.0;
    for (const PartSpec& spec : specs) {
        totalWeight += std::max(spec.initialWeight, minimumShare);
    }
    for (PartSpec& spec : specs) {
        Part part;
        part.resolvesOnDevice = spec.backend->resolvesHitsOnDevice();
        part.share = std::max(spec.initialWeight, minimumShare) / totalWeight;
        part.label = std::move(spec.label);
        part.memoryCapacity = spec.memoryCapacity;
        part.backend = std::move(spec.backend);
        deviceParts += part.resolvesOnDevice ? 1 : 0;
        parts.push_back(std::move(part));
    }
    if (deviceParts > 0) {
        for (Part& part : parts) {
            if (!part.resolvesOnDevice) {
                part.backend->shareCoresWithGpu();
            }
        }
    }
}

DeviceComposer::~DeviceComposer() = default;

BackendHandle DeviceComposer::create(std::vector<PartSpec> parts, Tuning tuning) {
    if (parts.empty() || std::ranges::any_of(parts, [](const PartSpec& part) { return !part.backend; })) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    return BackendHandle(new DeviceComposer(std::move(parts), tuning), BackendRelease{destroyComposer, nullptr});
}

size_t DeviceComposer::ballCount() const {
    size_t total = 0;
    for (const Part& part : parts) {
        total += part.backend->ballCount();
    }
    return total;
}

size_t DeviceComposer::partCapacity(const Part& part) const {
    const size_t limit = part.backend->ballCapacityLimit();
    return limit == std::numeric_limits<size_t>::max() && part.memoryCapacity > 0 ? part.memoryCapacity : limit;
}

size_t DeviceComposer::ballCapacityLimit() const {
    size_t limit = 0;
    for (const Part& part : parts) {
        limit = saturatingAdd(limit, partCapacity(part));
    }
    return limit;
}

void DeviceComposer::uploadBoard(const BoardUpload& board) {
    for (Part& part : parts) {
        part.backend->uploadBoard(board);
    }
    if (deviceParts >= 2 && !board.brickHitsLeft.empty()) {
        brickHits.assign(board.brickHitsLeft.begin(), board.brickHitsLeft.end());
    }
}

std::vector<size_t> DeviceComposer::targetCounts(size_t total) const {
    std::vector<size_t> targets(parts.size(), 0);
    const auto lead = std::ranges::max_element(parts, {}, &Part::share) - parts.begin();
    if (total < settings.minimumComposedBalls) {
        targets[static_cast<size_t>(lead)] = total;
        return targets;
    }
    size_t assigned = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        targets[index] = static_cast<size_t>(static_cast<double>(total) * parts[index].share);
        assigned += targets[index];
    }
    targets[static_cast<size_t>(lead)] += total - std::min(total, assigned);
    return targets;
}

// Fills each part up to its target, within its memory; what no target wants goes wherever there is room, the
// largest share first.
void DeviceComposer::addBalls(std::span<const BallSpawn> spawns) {
    const std::vector<size_t> targets = targetCounts(ballCount() + spawns.size());
    std::vector<size_t> room(parts.size());
    for (size_t index = 0; index < parts.size(); ++index) {
        const size_t limit = partCapacity(parts[index]);
        const size_t count = parts[index].backend->ballCount();
        room[index] = limit > count ? limit - count : 0;
    }
    std::vector<size_t> take(parts.size(), 0);
    size_t left = spawns.size();
    for (size_t index = 0; index < parts.size() && left > 0; ++index) {
        const size_t count = parts[index].backend->ballCount();
        const size_t wanted = targets[index] > count ? targets[index] - count : 0;
        take[index] = std::min({wanted, room[index], left});
        room[index] -= take[index];
        left -= take[index];
    }
    std::vector<size_t> byShare(parts.size());
    std::iota(byShare.begin(), byShare.end(), size_t{0});
    std::ranges::stable_sort(byShare, [&](size_t a, size_t b) { return parts[a].share > parts[b].share; });
    for (const size_t index : byShare) {
        const size_t extra = std::min(room[index], left);
        take[index] += extra;
        left -= extra;
    }
    size_t offset = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (take[index] > 0) {
            parts[index].backend->addBalls(spawns.subspan(offset, take[index]));
            offset += take[index];
        }
    }
}

void DeviceComposer::rebalance() {
    const bool settled = shareMeasurements >= settlingMeasurements;
    if (settled && stepsSinceMove < settledMoveCooldownSteps) {
        ++stepsSinceMove;
        return;
    }
    const double rebalanceTolerance = settled ? settledRebalanceTolerance : settlingRebalanceTolerance;
    const size_t movedBefore = ballsMoved;
    for (size_t move = 0; move + 1 < parts.size(); ++move) {
        const std::vector<size_t> targets = targetCounts(ballCount());
        size_t donor = 0;
        size_t receiver = 0;
        size_t excess = 0;
        size_t deficit = 0;
        for (size_t index = 0; index < parts.size(); ++index) {
            const size_t count = parts[index].backend->ballCount();
            if (count > targets[index] && count - targets[index] > excess) {
                excess = count - targets[index];
                donor = index;
            }
            if (targets[index] > count && targets[index] - count > deficit) {
                deficit = targets[index] - count;
                receiver = index;
            }
        }
        const size_t tolerance = std::max(
            settings.minimumRebalanceBalls, static_cast<size_t>(static_cast<double>(targets[receiver]) * rebalanceTolerance));
        const bool emptying = targets[donor] == 0 && excess > 0;
        if (donor == receiver || excess == 0 || deficit == 0 || (std::max(excess, deficit) <= tolerance && !emptying)) {
            break;
        }
        PhysicsBackend& source = *parts[donor].backend;
        PhysicsBackend& destination = *parts[receiver].backend;
        const size_t limit = partCapacity(parts[receiver]);
        const size_t room = limit > destination.ballCount() ? limit - destination.ballCount() : 0;
        const size_t moved = std::min({excess, deficit, maximumBallsMovedPerStep, room});
        if (moved == 0) {
            break;
        }
        ballsMoved += moved;
        if (source.takeCompactTail(moved, movedBalls)) {
            if (!destination.addCompact(movedBalls)) {
                source.addCompact(movedBalls);
                break;
            }
            continue;
        }
        destination.addBalls(source.takeTail(moved));
    }
    if (ballsMoved != movedBefore) {
        ++movingSteps;
        stepsSinceMove = 0;
    }
}

// Each measured part's share becomes its throughput over the measured parts' total, smoothed; parts with too few
// balls to time keep theirs.
void DeviceComposer::updateShares() {
    double measuredShare = 0.0;
    double measuredSpeed = 0.0;
    size_t measured = 0;
    const auto measurable = [this](const Part& part) {
        return !part.parked && part.steppedBalls >= settings.minimumBallsToMeasure && part.lastMs > 0.0;
    };
    for (const Part& part : parts) {
        if (measurable(part)) {
            measuredShare += part.share;
            measuredSpeed += static_cast<double>(part.steppedBalls) / part.lastMs;
            ++measured;
        }
    }
    if (measured < 2 || measuredSpeed <= 0.0) {
        return;
    }
    const double smoothing = shareMeasurements < settlingMeasurements ? settlingSmoothing : shareSmoothing;
    ++shareMeasurements;
    for (Part& part : parts) {
        if (measurable(part)) {
            const double balanced = measuredShare * (static_cast<double>(part.steppedBalls) / part.lastMs) / measuredSpeed;
            part.share += smoothing * (std::max(balanced, minimumShare) - part.share);
        }
    }
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0,
                                         [](double sum, const Part& part) { return sum + part.share; });
    for (Part& part : parts) {
        part.share /= total;
    }
}

// Equal step times are the best split only while every part's time shrinks with its share. A part with a large
// fixed cost per step (thread wake-ups, a whole visibility grid to fill, a driver round trip) can make the
// composed step slower than the lead part would be on its own; such a part is parked: its balls move away and it
// stops stepping. Compared on smoothed times, once the shares have settled.
void DeviceComposer::parkLosingParts(double wallMs) {
    if (!settings.parkLosingParts || shareMeasurements < settlingMeasurements) {
        return;
    }
    const auto lead = std::ranges::max_element(parts, {}, &Part::share);
    size_t stepped = 0;
    for (const Part& part : parts) {
        stepped += part.steppedBalls;
    }
    if (lead->steppedBalls < settings.minimumBallsToMeasure || lead->lastMs <= 0.0) {
        return;
    }
    const double soloMs = lead->lastMs * static_cast<double>(stepped) / static_cast<double>(lead->steppedBalls);
    wallEwma = wallEwma <= 0.0 ? wallMs : wallEwma + wallSmoothing * (wallMs - wallEwma);
    soloEwma = soloEwma <= 0.0 ? soloMs : soloEwma + wallSmoothing * (soloMs - soloEwma);
    losingSteps = wallEwma > soloEwma * (1.0 + parkMargin) ? losingSteps + 1 : 0;
    if (losingSteps < parkAfterSteps) {
        return;
    }
    Part* slowest = nullptr;
    for (Part& part : parts) {
        if (&part == &*lead || part.parked || part.lastMs <= 0.0) {
            continue;
        }
        const double speed = static_cast<double>(part.steppedBalls) / part.lastMs;
        if (!slowest || speed < static_cast<double>(slowest->steppedBalls) / slowest->lastMs) {
            slowest = &part;
        }
    }
    if (!slowest) {
        return;
    }
    slowest->parked = true;
    slowest->share = 0.0;
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0,
                                         [](double sum, const Part& part) { return sum + part.share; });
    for (Part& part : parts) {
        part.share /= total;
    }
    shareMeasurements = 0;
    losingSteps = 0;
    wallEwma = 0.0;
    soloEwma = 0.0;
}

// Splits a brick's remaining hits between the device-resolving parts in proportion to their shares of the balls,
// so their budgets add up to exactly what the brick has. The units left after rounding go to a part drawn by hash,
// so on average every part gets its proportional chance at the last hit of a brick. Every brick is drawn again each
// tick: a brick's last hit drawn to a part whose balls never reach it would otherwise stay there and keep the brick
// alive. Only parts whose budget changed are sent it: the others' device copies already match.
void DeviceComposer::splitBrickBudgets() {
    std::vector<double> weights(deviceParts);
    double totalWeight = 0.0;
    for (size_t index = 0; index < deviceParts; ++index) {
        weights[index] = parts[index].backend->ballCount() > 0 ? parts[index].share : 0.0;
        totalWeight += weights[index];
    }
    if (totalWeight <= 0.0) {
        std::ranges::fill(weights, 1.0);
        totalWeight = static_cast<double>(deviceParts);
    }
    std::vector<bool> changed(deviceParts, false);
    for (size_t index = 0; index < deviceParts; ++index) {
        if (parts[index].budget.size() != brickHits.size()) {
            parts[index].budget.assign(brickHits.size(), 0);
            changed[index] = true;
        }
    }
    ++budgetEpoch;
    std::vector<int32_t> split(deviceParts);
    for (size_t brick = 0; brick < brickHits.size(); ++brick) {
        const int32_t hits = std::max(brickHits[brick], 0);
        int32_t assigned = 0;
        for (size_t index = 0; index < deviceParts; ++index) {
            split[index] = static_cast<int32_t>(static_cast<double>(hits) * weights[index] / totalWeight);
            assigned += split[index];
        }
        for (int32_t unit = 0; assigned < hits; ++unit, ++assigned) {
            const uint32_t draw = mixHash(static_cast<uint32_t>(brick) ^ mixHash(budgetEpoch + static_cast<uint32_t>(unit) * 0x51ed27u));
            double pick = static_cast<double>(draw) / slotSelectionRange * totalWeight;
            size_t chosen = deviceParts - 1;
            for (size_t index = 0; index < deviceParts; ++index) {
                if (pick < weights[index]) {
                    chosen = index;
                    break;
                }
                pick -= weights[index];
            }
            ++split[chosen];
        }
        for (size_t index = 0; index < deviceParts; ++index) {
            changed[index] = changed[index] || parts[index].budget[brick] != split[index];
            parts[index].budget[brick] = split[index];
        }
    }
    for (size_t index = 0; index < deviceParts; ++index) {
        if (changed[index]) {
            parts[index].backend->uploadBrickHits(parts[index].budget);
        }
    }
}

// What each device part consumed from its budget comes off the true counts. CUDA lists the bricks it hit, Vulkan
// returns its remaining counts; either gives the consumption per brick.
void DeviceComposer::mergeDeviceHits() {
    mergedHits = {};
    BallCount killed = 0;
    for (size_t index = 0; index < deviceParts; ++index) {
        const DeviceHitResults results = parts[index].backend->deviceHitResults();
        if (results.killed == 0) {
            continue;
        }
        killed += results.killed;
        for (int color = 0; color < ballColorCount; ++color) {
            mergedHits.killsByColor[static_cast<size_t>(color)] += results.killsByColor[static_cast<size_t>(color)];
        }
        // The budget follows the device's own copy, so the next split sends only what differs from it.
        std::vector<int32_t>& budget = parts[index].budget;
        if (!results.brickHitsLeft.empty()) {
            const size_t bricks = std::min({results.brickHitsLeft.size(), budget.size(), brickHits.size()});
            for (size_t brick = 0; brick < bricks; ++brick) {
                if (const int32_t consumed = budget[brick] - results.brickHitsLeft[brick]; consumed > 0) {
                    brickHits[brick] -= consumed;
                    budget[brick] -= consumed;
                }
            }
        } else {
            for (const uint32_t brick : results.hitBricks) {
                if (brick < brickHits.size()) {
                    --brickHits[brick];
                    if (brick < budget.size()) {
                        --budget[brick];
                    }
                }
            }
        }
    }
    if (killed == 0) {
        return;
    }
    for (int32_t& hits : brickHits) {
        hits = std::max(hits, 0);
    }
    mergedHitsLeft = brickHits;
    mergedHits.brickHitsLeft = mergedHitsLeft;
    mergedHits.killed = killed;
}

DeviceHitResults DeviceComposer::deviceHitResults() const {
    if (deviceParts == 1) {
        return parts.front().backend->deviceHitResults();
    }
    return deviceParts >= 2 ? mergedHits : DeviceHitResults{};
}

void DeviceComposer::step(const StepParams& params, std::vector<BallHit>& hits) {
    const Clock::time_point stepStart = Clock::now();
    // In free flight no part steps, so there is no throughput to balance.
    if (!params.freeFlight) {
        rebalance();
    }
    stepGridWidth = gridWidth;
    stepGridHeight = gridHeight;
    lastBudgetMs = 0.0;
    if (deviceParts >= 2 && !brickHits.empty()) {
        const Clock::time_point start = Clock::now();
        splitBrickBudgets();
        lastBudgetMs += millisecondsSince(start);
    }
    const auto stepPart = [&params](Part& part) {
        const Clock::time_point start = Clock::now();
        part.backend->step(params, part.hits);
        part.lastMs = millisecondsSince(start);
    };
    std::vector<std::future<void>> helpers;
    helpers.reserve(parts.size());
    for (size_t index = 0; index < parts.size(); ++index) {
        Part& part = parts[index];
        part.hits.clear();
        part.steppedBalls = part.backend->ballCount();
        part.lastMs = 0.0;
        if (part.steppedBalls > 0 && index > 0) {
            helpers.push_back(std::async(std::launch::async, stepPart, std::ref(part)));
        }
    }
    if (parts.front().steppedBalls > 0) {
        stepPart(parts.front());
    }
    for (std::future<void>& helper : helpers) {
        helper.get();
    }
    if (!params.freeFlight) {
        updateShares();
        parkLosingParts(millisecondsSince(stepStart));
    }
    if (deviceParts >= 2) {
        const Clock::time_point start = Clock::now();
        mergeDeviceHits();
        lastBudgetMs += millisecondsSince(start);
        budgetMsTotal += lastBudgetMs;
        ++budgetSteps;
    }
    uint32_t offset = 0;
    for (const Part& part : parts) {
        for (const BallHit& hit : part.hits) {
            hits.push_back({hit.ball + offset, hit.brick});
        }
        offset += static_cast<uint32_t>(part.backend->ballCount());
    }
}

void DeviceComposer::removeBalls(std::span<const uint32_t> descendingIndices) {
    std::vector<uint32_t> local;
    uint32_t partEnd = static_cast<uint32_t>(ballCount());
    size_t next = 0;
    for (size_t index = parts.size(); index-- > 0;) {
        const uint32_t partStart = partEnd - static_cast<uint32_t>(parts[index].backend->ballCount());
        local.clear();
        while (next < descendingIndices.size() && descendingIndices[next] >= partStart) {
            local.push_back(descendingIndices[next] - partStart);
            ++next;
        }
        if (!local.empty()) {
            parts[index].backend->removeBalls(local);
        }
        partEnd = partStart;
    }
}

void DeviceComposer::overlay(const BallSnapshot& balls, uint32_t replaceBelow, uint32_t salt) {
    if (!balls.packed || balls.count == 0) {
        return;
    }
    const auto place = [&](size_t slot, uint32_t value) {
        if (merged[slot] == emptyRenderSlot || mixHash(static_cast<uint32_t>(slot) ^ salt) < replaceBelow) {
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

// Every part fills the same visibility grid. Part k wins an occupied slot with probability (its balls) / (balls of
// parts 0..k), which leaves each slot showing any part's ball in proportion to how many balls that part has.
BallSnapshot DeviceComposer::snapshot() {
    std::vector<BallSnapshot> snapshots;
    snapshots.reserve(parts.size());
    bool anyGrid = false;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool fieldKnown = false;
    for (Part& part : parts) {
        snapshots.push_back(part.backend->ballCount() > 0 ? part.backend->snapshot() : BallSnapshot{});
        const BallSnapshot& balls = snapshots.back();
        anyGrid = anyGrid || balls.visibilityGrid;
        if (balls.packed && !fieldKnown) {
            fieldWidth = balls.fieldWidth;
            fieldHeight = balls.fieldHeight;
            fieldKnown = true;
        }
    }
    if (!anyGrid) {
        merged.clear();
        for (const BallSnapshot& balls : snapshots) {
            if (balls.packed) {
                merged.insert(merged.end(), balls.packed, balls.packed + balls.count);
            }
        }
        return {merged.data(), merged.size(), fieldWidth, fieldHeight, false, false};
    }
    merged.assign(gridCellCount(), emptyRenderSlot);
    if (merged.empty()) {
        return {};
    }
    double seen = 0.0;
    for (size_t index = 0; index < parts.size(); ++index) {
        const double count = static_cast<double>(parts[index].backend->ballCount());
        if (count <= 0.0) {
            continue;
        }
        seen += count;
        const uint32_t replaceBelow =
            seen == count ? 0u
                          : static_cast<uint32_t>(std::min(count / seen * slotSelectionRange,
                                                           static_cast<double>(std::numeric_limits<uint32_t>::max())));
        overlay(snapshots[index], replaceBelow, mixHash(static_cast<uint32_t>(index) + 1u));
    }
    return {merged.data(), merged.size(), fieldWidth, fieldHeight, false, true};
}

std::vector<BallSpawn> DeviceComposer::exportBalls() {
    std::vector<BallSpawn> balls;
    for (Part& part : parts) {
        const std::vector<BallSpawn> partBalls = part.backend->exportBalls();
        balls.insert(balls.end(), partBalls.begin(), partBalls.end());
    }
    return balls;
}

std::vector<BallSpawn> DeviceComposer::takeTail(size_t count) {
    std::vector<BallSpawn> balls;
    for (size_t index = parts.size(); index-- > 0 && balls.size() < count;) {
        const std::vector<BallSpawn> partBalls = parts[index].backend->takeTail(count - balls.size());
        balls.insert(balls.end(), partBalls.begin(), partBalls.end());
    }
    return balls;
}

void DeviceComposer::clear() {
    for (Part& part : parts) {
        part.backend->clear();
    }
}

BallTally DeviceComposer::truncate(size_t newCount) {
    const size_t total = ballCount();
    if (newCount >= total) {
        return {};
    }
    std::vector<size_t> keep(parts.size());
    size_t kept = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        const size_t count = parts[index].backend->ballCount();
        keep[index] = static_cast<size_t>(static_cast<double>(newCount) * static_cast<double>(count) /
                                          static_cast<double>(total));
        keep[index] = std::min(keep[index], count);
        kept += keep[index];
    }
    for (size_t index = 0; index < parts.size() && kept < newCount; ++index) {
        const size_t extra = std::min(parts[index].backend->ballCount() - keep[index], newCount - kept);
        keep[index] += extra;
        kept += extra;
    }
    BallTally removed{};
    for (size_t index = 0; index < parts.size(); ++index) {
        const BallTally partRemoved = parts[index].backend->truncate(keep[index]);
        for (size_t color = 0; color < removed.size(); ++color) {
            removed[color] += partRemoved[color];
        }
    }
    return removed;
}

std::string_view DeviceComposer::faultMessage() const {
    for (const Part& part : parts) {
        if (const std::string_view fault = part.backend->faultMessage(); !fault.empty()) {
            return fault;
        }
    }
    return {};
}

void DeviceComposer::setVisibilityGrid(int width, int height) {
    gridWidth = std::max(width, 0);
    gridHeight = std::max(height, 0);
    for (Part& part : parts) {
        part.backend->setVisibilityGrid(width, height);
    }
}

double DeviceComposer::deviceStepMs() const {
    double slowest = 0.0;
    for (const Part& part : parts) {
        slowest = std::max(slowest, part.backend->deviceStepMs());
    }
    return slowest;
}
