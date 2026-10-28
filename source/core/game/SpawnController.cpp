#include "game/SpawnController.h"

#include "sim/Simulation.h"

#include <algorithm>

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

BallCount SpawnController::update(float deltaTime, Simulation& simulation, BallCount capacityLeft,
                                  BallCount frameAllowance) {
    if (streams.empty()) {
        budget = 0.0;
        return 0;
    }
    const bool unlimited = ballsPerSecond <= 0.0f;
    budget = unlimited ? static_cast<double>(frameAllowance) : budget + static_cast<double>(ballsPerSecond) * deltaTime;
    BallCount allowance = std::min({static_cast<BallCount>(budget), frameAllowance, capacityLeft});
    budget -= static_cast<double>(allowance);
    const BallCount launched = allowance;
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
    return launched - allowance;
}
