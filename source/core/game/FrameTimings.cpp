#include "game/FrameTimings.h"

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
    total.presented = 0.0;
    worstFrameMs = 0.0;
    worstWorkMs = 0.0;
    for (const FrameSample& entry : window) {
        total.frameMs += entry.frameMs;
        total.workMs += entry.workMs;
        total.cpuMs += entry.cpuMs;
        total.renderMs += entry.renderMs;
        total.gpuMs += entry.gpuMs;
        total.presentMs += entry.presentMs;
        total.presented += entry.presented;
        total.waitMs += entry.waitMs;
        worstFrameMs = std::max(worstFrameMs, entry.frameMs);
        worstWorkMs = std::max(worstWorkMs, entry.workMs);
    }
    const double count = static_cast<double>(window.size());
    averaged = {total.frameMs / count,   total.workMs / count,    total.cpuMs / count,
                total.renderMs / count,  total.gpuMs / count,     total.presentMs / count,
                total.presented / count, total.waitMs / count};
}
