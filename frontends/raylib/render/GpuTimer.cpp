#include "render/GpuTimer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C" void glfwSwapInterval(int interval);

namespace {

constexpr unsigned int glTimeElapsed = 0x88BF;
constexpr unsigned int glQueryResult = 0x8866;
constexpr unsigned int glQueryResultAvailable = 0x8867;
constexpr double nanosecondsPerMillisecond = 1.0e6;

using GenQueries = void(APIENTRY*)(int, unsigned int*);
using BeginQuery = void(APIENTRY*)(unsigned int, unsigned int);
using EndQuery = void(APIENTRY*)(unsigned int);
using GetQueryObjectInt = void(APIENTRY*)(unsigned int, unsigned int, int*);
using GetQueryObjectUint64 = void(APIENTRY*)(unsigned int, unsigned int, unsigned long long*);

GenQueries genQueries = nullptr;
BeginQuery beginQuery = nullptr;
EndQuery endQuery = nullptr;
GetQueryObjectInt getQueryObjectInt = nullptr;
GetQueryObjectUint64 getQueryObjectUint64 = nullptr;

template <typename Function>
bool loadGlFunction(const char* name, Function& target) {
    target = reinterpret_cast<Function>(reinterpret_cast<void*>(wglGetProcAddress(name)));
    return target != nullptr;
}

}

bool setSwapInterval(int interval) {
    glfwSwapInterval(interval);
    return true;
}

bool GpuTimer::load() {
    available = loadGlFunction("glGenQueries", genQueries) && loadGlFunction("glBeginQuery", beginQuery) &&
                loadGlFunction("glEndQuery", endQuery) &&
                loadGlFunction("glGetQueryObjectiv", getQueryObjectInt) &&
                loadGlFunction("glGetQueryObjectui64v", getQueryObjectUint64);
    if (available) {
        genQueries(static_cast<int>(queryRingSize), queries.data());
    }
    return available;
}

void GpuTimer::beginFrame() {
    if (!available) {
        return;
    }
    for (size_t i = 0; i < queryRingSize; ++i) {
        if (!pending[i]) {
            continue;
        }
        int ready = 0;
        getQueryObjectInt(queries[i], glQueryResultAvailable, &ready);
        if (ready) {
            unsigned long long nanoseconds = 0;
            getQueryObjectUint64(queries[i], glQueryResult, &nanoseconds);
            lastMs = static_cast<double>(nanoseconds) / nanosecondsPerMillisecond;
            pending[i] = false;
        }
    }
    if (pending[writeIndex]) {
        return;
    }
    beginQuery(glTimeElapsed, queries[writeIndex]);
    frameOpen = true;
}

void GpuTimer::endFrame() {
    if (!available || !frameOpen) {
        return;
    }
    endQuery(glTimeElapsed);
    pending[writeIndex] = true;
    writeIndex = (writeIndex + 1) % queryRingSize;
    frameOpen = false;
}
