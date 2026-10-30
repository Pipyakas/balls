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
// Steps whose times are added up before a share is measured from them. One step's time on a board where most balls
// hit bricks varies threefold for the CPU (hits, freshly moved unsorted balls), and shares measured per step made the
// CPU's balls swing between 4M and 21M of 60M from frame to frame on the target PC, each swing a costly move.
constexpr int settlingWindowSteps = 4;
constexpr int settledWindowSteps = 16;
constexpr double slotSelectionRange = 4294967296.0;

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

// Sparse updates set a brick's hits on every part to the host's count. With two or more device parts each holds its
// own budget instead, so they take whole boards and get their budgets drawn from those.
bool DeviceComposer::acceptsSparseBoardUpdates() const {
    return deviceParts < 2 &&
           std::ranges::all_of(parts, [](const Part& part) { return part.backend->acceptsSparseBoardUpdates(); });
}

void DeviceComposer::setDrawing(bool drawing) {
    for (Part& part : parts) {
        part.backend->setDrawing(drawing);
    }
}

// Each part fills the visibility grid from a sample of its balls; a part holding a fraction of the balls only wins
// that fraction of the slots in snapshot(), so it needs only that fraction of the samples.
void DeviceComposer::shareRenderSamples() {
    const size_t total = ballCount();
    for (Part& part : parts) {
        part.backend->setRenderSampleShare(
            total > 0 ? static_cast<double>(part.backend->ballCount()) / static_cast<double>(total) : 1.0);
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
    for (Part& part : parts) {
        part.windowBalls += part.steppedBalls;
        part.windowMs += part.lastMs;
    }
    const bool settling = shareMeasurements < settlingMeasurements;
    if (++windowSteps < (settling ? settlingWindowSteps : settledWindowSteps)) {
        return;
    }
    windowSteps = 0;
    double measuredShare = 0.0;
    double measuredSpeed = 0.0;
    size_t measured = 0;
    const auto measurable = [this](const Part& part) {
        return part.windowBalls >= settings.minimumBallsToMeasure && part.windowMs > 0.0;
    };
    const auto speed = [](const Part& part) { return static_cast<double>(part.windowBalls) / part.windowMs; };
    for (const Part& part : parts) {
        if (measurable(part)) {
            measuredShare += part.share;
            measuredSpeed += speed(part);
            ++measured;
        }
    }
    if (measured < 2 || measuredSpeed <= 0.0) {
        for (Part& part : parts) {
            part.windowBalls = 0;
            part.windowMs = 0.0;
        }
        return;
    }
    const double smoothing = shareMeasurements < settlingMeasurements ? settlingSmoothing : shareSmoothing;
    ++shareMeasurements;
    for (Part& part : parts) {
        if (measurable(part)) {
            const double balanced = measuredShare * speed(part) / measuredSpeed;
            part.share += smoothing * (std::max(balanced, minimumShare) - part.share);
        }
        part.windowBalls = 0;
        part.windowMs = 0.0;
    }
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0,
                                         [](double sum, const Part& part) { return sum + part.share; });
    for (Part& part : parts) {
        part.share /= total;
    }
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
    // In free flight no part steps, so there is no throughput to balance.
    if (!params.freeFlight) {
        rebalance();
    }
    stepGridWidth = gridWidth;
    stepGridHeight = gridHeight;
    shareRenderSamples();
    lastBudgetMs = 0.0;
    if (deviceParts >= 2 && !brickHits.empty()) {
        const Clock::time_point start = Clock::now();
        splitBrickBudgets();
        lastBudgetMs += millisecondsSince(start);
    }
    // A part that is itself a composer (VulkanMulti) may move balls between its devices inside its step; that step
    // says nothing about its speed, and timing it would shift balls away from it, so it goes unmeasured.
    const auto stepPart = [&params](Part& part) {
        const auto* nested = dynamic_cast<const DeviceComposer*>(part.backend.get());
        const size_t movesBefore = nested ? nested->movingStepCount() : 0;
        const Clock::time_point start = Clock::now();
        part.backend->step(params, part.hits);
        part.lastMs = nested && nested->movingStepCount() != movesBefore ? 0.0 : millisecondsSince(start);
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
    // With the other parts parked or empty, one part holds every ball and its snapshot is already the merge.
    if (std::ranges::count_if(parts, [](const Part& part) { return part.backend->ballCount() > 0; }) == 1) {
        return std::ranges::find_if(parts, [](const Part& part) { return part.backend->ballCount() > 0; })
            ->backend->snapshot();
    }
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

bool DeviceComposer::takeCompactTail(size_t count, CompactBalls& taken) {
    size_t remaining = std::min(count, ballCount());
    std::vector<uint32_t> positions;
    std::vector<uint32_t> states;
    for (size_t index = parts.size(); index-- > 0 && remaining > 0;) {
        const size_t take = std::min(remaining, parts[index].backend->ballCount());
        if (take == 0) {
            continue;
        }
        CompactBalls partBalls;
        if (!parts[index].backend->takeCompactTail(take, partBalls)) {
            return false;
        }
        positions.insert(positions.end(), partBalls.position.begin(), partBalls.position.end());
        states.insert(states.end(), partBalls.state.begin(), partBalls.state.end());
        remaining -= take;
    }
    taken.position = std::move(positions);
    taken.state = std::move(states);
    return true;
}

bool DeviceComposer::addCompact(const CompactBalls& added) {
    const size_t addedCount = std::min(added.position.size(), added.state.size());
    if (addedCount == 0) {
        return true;
    }
    const std::vector<size_t> targets = targetCounts(ballCount() + addedCount);
    std::vector<size_t> take(parts.size(), 0);
    size_t left = addedCount;
    for (size_t index = 0; index < parts.size() && left > 0; ++index) {
        const size_t count = parts[index].backend->ballCount();
        const size_t wanted = targets[index] > count ? targets[index] - count : 0;
        take[index] = std::min({wanted, left});
        left -= take[index];
    }
    std::vector<size_t> byShare(parts.size());
    std::iota(byShare.begin(), byShare.end(), size_t{0});
    std::ranges::stable_sort(byShare, [&](size_t a, size_t b) { return parts[a].share > parts[b].share; });
    for (const size_t index : byShare) {
        if (left == 0) {
            break;
        }
        take[index] += left;
        left = 0;
    }
    size_t offset = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (take[index] == 0) {
            continue;
        }
        CompactBalls slice;
        slice.position.assign(added.position.begin() + static_cast<std::ptrdiff_t>(offset),
                              added.position.begin() + static_cast<std::ptrdiff_t>(offset + take[index]));
        slice.state.assign(added.state.begin() + static_cast<std::ptrdiff_t>(offset),
                           added.state.begin() + static_cast<std::ptrdiff_t>(offset + take[index]));
        if (!parts[index].backend->addCompact(slice)) {
            return false;
        }
        offset += take[index];
    }
    return offset == addedCount;
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
