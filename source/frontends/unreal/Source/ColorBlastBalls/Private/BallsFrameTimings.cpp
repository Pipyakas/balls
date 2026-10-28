#include "BallsFrameTimings.h"

namespace {

constexpr double WindowLengthMs = 1000.0;

}

void FBallsFrameTimings::Record(const FBallsFrameSample& Sample) {
    Window.Add(Sample);
    WindowMs += Sample.FrameMs;
    TotalWorkMs += Sample.WorkMs();
    int32 Expired = 0;
    while (Window.Num() - Expired > 1 && WindowMs - Window[Expired].FrameMs >= WindowLengthMs) {
        WindowMs -= Window[Expired].FrameMs;
        TotalWorkMs -= Window[Expired].WorkMs();
        ++Expired;
    }
    Window.RemoveAt(0, Expired, EAllowShrinking::No);

    FBallsFrameSample Total;
    WorstFrameMs = 0.0;
    WorstWorkMs = 0.0;
    for (const FBallsFrameSample& Entry : Window) {
        Total.FrameMs += Entry.FrameMs;
        Total.GameMs += Entry.GameMs;
        Total.RenderMs += Entry.RenderMs;
        Total.GpuMs += Entry.GpuMs;
        WorstFrameMs = FMath::Max(WorstFrameMs, Entry.FrameMs);
        WorstWorkMs = FMath::Max(WorstWorkMs, Entry.WorkMs());
    }
    const double Count = static_cast<double>(Window.Num());
    AverageWorkMs = TotalWorkMs / Count;
    Averaged = {Total.FrameMs / Count, Total.GameMs / Count, Total.RenderMs / Count, Total.GpuMs / Count};
}
