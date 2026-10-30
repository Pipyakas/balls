#pragma once

#include <deque>

// One rendered frame as a front end measured it. Fields a front end cannot measure stay 0.
struct FrameSample {
    double frameMs = 0.0;
    // What the frame costs the machine when the display never makes it wait: what ball filling uses, so VSync,
    // a frame cap or a Remote Desktop compositor do not cap what it measures. Each front end knows its own waits.
    double workMs = 0.0;
    // CPU time of the frame (SDL), or the game thread (Unreal).
    double cpuMs = 0.0;
    // The render thread, where the engine has one.
    double renderMs = 0.0;
    double gpuMs = 0.0;
    double presentMs = 0.0;
    // 1 when the frame reached the screen, 0 when the compositor was not ready; averages to the shown share.
    double presented = 1.0;
    // Time spent waiting rather than working: SDL's present wait, Unreal's frame-rate cap sleep.
    double waitMs = 0.0;
};

// A rolling one-second window of frame samples: the averages and worst frames the debug gadget shows.
class FrameTimings {
public:
    void record(const FrameSample& sample);

    const FrameSample& average() const { return averaged; }
    const FrameSample& latest() const { return newest; }
    double framesPerSecond() const { return averaged.frameMs > 0.0 ? 1000.0 / averaged.frameMs : 0.0; }
    double workFramesPerSecond() const { return averaged.workMs > 0.0 ? 1000.0 / averaged.workMs : 0.0; }
    double shownFramesPerSecond() const { return framesPerSecond() * averaged.presented; }
    double worstFpsLastSecond() const { return worstFrameMs > 0.0 ? 1000.0 / worstFrameMs : 0.0; }
    double worstWorkFpsLastSecond() const { return worstWorkMs > 0.0 ? 1000.0 / worstWorkMs : 0.0; }
    double worstFrameMsLastSecond() const { return worstFrameMs; }

private:
    std::deque<FrameSample> window;
    double windowMs = 0.0;
    FrameSample averaged;
    FrameSample newest;
    double worstFrameMs = 0.0;
    double worstWorkMs = 0.0;
};
