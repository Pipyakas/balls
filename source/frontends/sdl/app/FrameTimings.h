#pragma once

#include <deque>

struct FrameSample {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    double presentMs = 0.0;
    // 1 when the frame reached the screen, 0 when the compositor was not ready; averages to the shown share.
    double presented = 1.0;
    double presentWaitMs = 0.0;

    double workMs() const { return frameMs > presentWaitMs ? frameMs - presentWaitMs : 0.0; }
};

class FrameTimings {
public:
    void record(const FrameSample& sample);

    const FrameSample& average() const { return averaged; }
    const FrameSample& latest() const { return newest; }
    double framesPerSecond() const { return averaged.frameMs > 0.0 ? 1000.0 / averaged.frameMs : 0.0; }
    // The frame rate the game sustains when the display never makes it wait: what the capacity probe uses, so
    // VSync or a Remote Desktop compositor do not cap what it measures.
    double workFramesPerSecond() const { return averaged.workMs() > 0.0 ? 1000.0 / averaged.workMs() : 0.0; }
    double worstWorkFpsLastSecond() const { return worstWorkMs > 0.0 ? 1000.0 / worstWorkMs : 0.0; }
    double shownFramesPerSecond() const { return framesPerSecond() * averaged.presented; }
    double worstFpsLastSecond() const { return worstFrameMs > 0.0 ? 1000.0 / worstFrameMs : 0.0; }
    double worstFrameMsLastSecond() const { return worstFrameMs; }

private:
    std::deque<FrameSample> window;
    double windowMs = 0.0;
    FrameSample averaged;
    FrameSample newest;
    double worstFrameMs = 0.0;
    double worstWorkMs = 0.0;
};
