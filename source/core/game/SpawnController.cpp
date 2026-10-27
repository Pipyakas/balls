#include "game/SpawnController.h"

#include "sim/Simulation.h"

#include <algorithm>

namespace {

constexpr BallCount maxBallsPerFrame = 1 << 20;

}

void SpawnController::enqueue(BallColor color, BallCount count) {
    if (count == 0) {
        return;
    }
    streams.push_back({color, count});
    pending = pending > unlimitedBalls - count ? unlimitedBalls : pending + count;
}

void SpawnController::clear() {
    streams.clear();
    pending = 0;
    budget = 0.0;
}

void SpawnController::update(float deltaTime, Simulation& simulation, BallCount capacityLeft) {
    if (streams.empty()) {
        budget = 0.0;
        return;
    }
    const bool unlimited = ballsPerSecond <= 0.0f;
    budget = unlimited ? static_cast<double>(maxBallsPerFrame) : budget + static_cast<double>(ballsPerSecond) * deltaTime;
    BallCount allowance = std::min({static_cast<BallCount>(budget), maxBallsPerFrame, capacityLeft});
    budget -= static_cast<double>(allowance);
    while (allowance > 0 && !streams.empty()) {
        Stream& stream = streams.front();
        const BallCount released = std::min(allowance, stream.remaining);
        simulation.launchBalls(stream.color, released);
        stream.remaining -= released;
        allowance -= released;
        pending -= released;
        if (stream.remaining == 0) {
            streams.pop_front();
        }
    }
    if (unlimited || streams.empty()) {
        budget = 0.0;
    }
}
