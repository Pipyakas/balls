#pragma once

#include "common/BallColor.h"

#include <deque>

class Simulation;

class SpawnController {
public:
    void enqueue(BallColor color, long long count);
    void setBallsPerSecond(float rate) { ballsPerSecond = rate; }
    void update(float deltaTime, Simulation& simulation);
    void clear();

    long long pendingBalls() const { return pending; }
    bool isIdle() const { return streams.empty(); }

private:
    struct Stream {
        BallColor color;
        long long remaining;
    };

    std::deque<Stream> streams;
    float ballsPerSecond = 60.0f;
    double budget = 0.0;
    long long pending = 0;
};
