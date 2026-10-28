#pragma once

#include "CoreMinimal.h"

struct FBallsFrameSample {
    double FrameMs = 0.0;
    double GameMs = 0.0;
    double RenderMs = 0.0;
    double GpuMs = 0.0;
    // Time the engine slept to hold a frame-rate cap (t.MaxFPS): the one wait that is not the game's own work.
    double IdleMs = 0.0;

    // What the frame costs the machine when nothing makes it wait for the display: the limiting thread, or the whole
    // frame minus the cap's sleep when that is longer. The thread times alone would hide a stall (memory paging, a
    // long core step on a worker) that makes the real frame far longer than any thread was busy.
    double WorkMs() const {
        return FMath::Max(FMath::Max3(GameMs, RenderMs, GpuMs), FMath::Max(0.0, FrameMs - IdleMs));
    }
};

// Rolling one-second window of frame timings. Mirrors frontends/raylib/app/FrameTimings.
class FBallsFrameTimings {
public:
    void Record(const FBallsFrameSample& Sample);

    const FBallsFrameSample& Average() const { return Averaged; }
    double FramesPerSecond() const { return Averaged.FrameMs > 0.0 ? 1000.0 / Averaged.FrameMs : 0.0; }
    double WorstFpsLastSecond() const { return WorstFrameMs > 0.0 ? 1000.0 / WorstFrameMs : 0.0; }
    double WorkFramesPerSecond() const { return AverageWorkMs > 0.0 ? 1000.0 / AverageWorkMs : 0.0; }
    double WorstWorkFpsLastSecond() const { return WorstWorkMs > 0.0 ? 1000.0 / WorstWorkMs : 0.0; }

private:
    TArray<FBallsFrameSample> Window;
    double WindowMs = 0.0;
    double TotalWorkMs = 0.0;
    double AverageWorkMs = 0.0;
    double WorstFrameMs = 0.0;
    double WorstWorkMs = 0.0;
    FBallsFrameSample Averaged;
};
