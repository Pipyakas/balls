#include "app/FrameTimings.h"

#include <algorithm>

namespace {

constexpr double windowLengthMs = 1000.0;

}

void FrameTimings::record(const FrameSample& sample) {
    newest = sample;
    window.push_back(sample);
    windowMs += sample.frameMs;
    while (window.size() > 1 && windowMs - window.front().frameMs >= windowLengthMs) {
        windowMs -= window.front().frameMs;
        window.pop_front();
    }
    FrameSample total;
    worstFrameMs = 0.0;
    for (const FrameSample& entry : window) {
        total.frameMs += entry.frameMs;
        total.cpuMs += entry.cpuMs;
        total.gpuMs += entry.gpuMs;
        total.presentMs += entry.presentMs;
        worstFrameMs = std::max(worstFrameMs, entry.frameMs);
    }
    const double count = static_cast<double>(window.size());
    averaged = {total.frameMs / count, total.cpuMs / count, total.gpuMs / count, total.presentMs / count};
}
