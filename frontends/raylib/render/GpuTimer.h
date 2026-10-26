#pragma once

#include <array>
#include <cstdint>

bool setSwapInterval(int interval);

class GpuTimer {
public:
    bool load();
    void beginFrame();
    void endFrame();

    bool isAvailable() const { return available; }
    double lastFrameMs() const { return lastMs; }

private:
    static constexpr size_t queryRingSize = 4;

    std::array<uint32_t, queryRingSize> queries{};
    std::array<bool, queryRingSize> pending{};
    size_t writeIndex = 0;
    bool available = false;
    bool frameOpen = false;
    double lastMs = 0.0;
};
