#pragma once

#include "common/BallCount.h"

#include <cstddef>
#include <deque>

class Simulation;

class SpawnController {
public:
    void enqueue(BallColor color, BallCount count);

    void setBallsPerSecond(float rate) {
        ballsPerSecond = rate;
        budget = 0.0;
    }

    void setInterleaved(bool enabled) { interleaved = enabled; }

    BallCount accrue(float deltaTime, BallCount capacityLeft);

    BallCount release(Simulation& simulation, BallCount count);
    void clear();

    BallCount pendingBalls() const { return pending; }
    bool isIdle() const { return streams.empty(); }
    size_t activeStreams() const { return streams.size(); }

private:
    struct Stream {
        BallColor color;
        BallCount remaining;
    };

    std::deque<Stream> streams;
    float ballsPerSecond = 60.0f;
    bool interleaved = false;
    double budget = 0.0;
    BallCount pending = 0;
};
