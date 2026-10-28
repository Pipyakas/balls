#pragma once

#include "common/BallCount.h"

#include <deque>

class Simulation;

class SpawnController {
public:
    void enqueue(BallColor color, BallCount count);
    void setBallsPerSecond(float rate) { ballsPerSecond = rate; }
    // Returns how many balls it launched; frameAllowance caps them however fast the rate is.
    BallCount update(float deltaTime, Simulation& simulation, BallCount capacityLeft, BallCount frameAllowance);
    void clear();

    BallCount pendingBalls() const { return pending; }
    bool isIdle() const { return streams.empty(); }

private:
    struct Stream {
        BallColor color;
        BallCount remaining;
    };

    std::deque<Stream> streams;
    float ballsPerSecond = 60.0f;
    double budget = 0.0;
    BallCount pending = 0;
};
