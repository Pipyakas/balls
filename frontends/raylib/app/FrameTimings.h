#pragma once

#include <deque>

struct FrameSample {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    double presentMs = 0.0;
};

class FrameTimings {
public:
    void record(const FrameSample& sample);

    const FrameSample& average() const { return averaged; }
    const FrameSample& latest() const { return newest; }
    double framesPerSecond() const { return averaged.frameMs > 0.0 ? 1000.0 / averaged.frameMs : 0.0; }
    double worstFpsLastSecond() const { return worstFrameMs > 0.0 ? 1000.0 / worstFrameMs : 0.0; }
    double worstFrameMsLastSecond() const { return worstFrameMs; }

private:
    std::deque<FrameSample> window;
    double windowMs = 0.0;
    FrameSample averaged;
    FrameSample newest;
    double worstFrameMs = 0.0;
};
