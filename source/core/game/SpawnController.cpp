#include "game/SpawnController.h"

#include "sim/Simulation.h"

#include <algorithm>
#include <iterator>

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

BallCount SpawnController::accrue(float deltaTime, BallCount capacityLeft) {
    if (streams.empty()) {
        budget = 0.0;
        return 0;
    }
    budget = ballsPerSecond <= 0.0f ? static_cast<double>(capacityLeft)
                                    : budget + static_cast<double>(ballsPerSecond) * deltaTime;
    return std::min({static_cast<BallCount>(budget), capacityLeft, pending});
}

BallCount SpawnController::release(Simulation& simulation, BallCount count) {
    BallCount allowance = std::min(count, static_cast<BallCount>(budget));
    const BallCount released = allowance;
    while (allowance > 0 && !streams.empty()) {
        const BallCount share =
            interleaved ? std::max<BallCount>(1, allowance / static_cast<BallCount>(streams.size())) : allowance;
        for (auto stream = streams.begin(); stream != streams.end() && allowance > 0;) {
            const BallCount launched = std::min({share, allowance, stream->remaining});
            simulation.launchBalls(stream->color, launched);
            stream->remaining -= launched;
            allowance -= launched;
            pending -= launched;
            stream = stream->remaining == 0 ? streams.erase(stream) : std::next(stream);
        }
    }
    budget -= static_cast<double>(released - allowance);
    // A release whose balls do not cover every stream starts the next one with the stream after.
    if (interleaved && streams.size() > 1) {
        streams.push_back(streams.front());
        streams.pop_front();
    }
    if (streams.empty()) {
        budget = 0.0;
    }
    return released - allowance;
}
