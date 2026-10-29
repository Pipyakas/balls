#pragma once

#include "common/BallCount.h"

#include <cstddef>
#include <deque>

class Simulation;

class SpawnController {
public:
    void enqueue(BallColor color, BallCount count);
    void setBallsPerSecond(float rate) { ballsPerSecond = rate; }
    // Interleaved streams share each frame's balls instead of emptying one launcher before the next.
    void setInterleaved(bool enabled) { interleaved = enabled; }
    void update(float deltaTime, Simulation& simulation, BallCount capacityLeft);
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
