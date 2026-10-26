#pragma once

#include "CoreMinimal.h"

struct FBallsFrameSample {
    double FrameMs = 0.0;
    double GameMs = 0.0;
    double RenderMs = 0.0;
    double GpuMs = 0.0;
};

// Rolling one-second window of frame timings. Mirrors frontends/raylib/app/FrameTimings.
class FBallsFrameTimings {
public:
    void Record(const FBallsFrameSample& Sample);

    const FBallsFrameSample& Average() const { return Averaged; }
    double FramesPerSecond() const { return Averaged.FrameMs > 0.0 ? 1000.0 / Averaged.FrameMs : 0.0; }
    double WorstFpsLastSecond() const { return WorstFrameMs > 0.0 ? 1000.0 / WorstFrameMs : 0.0; }

private:
    TArray<FBallsFrameSample> Window;
    double WindowMs = 0.0;
    double WorstFrameMs = 0.0;
    FBallsFrameSample Averaged;
};
