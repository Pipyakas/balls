#include "render/GlExtras.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

constexpr unsigned int glPoints = 0x0000;
constexpr unsigned int glProgramPointSize = 0x8642;

using VertexAttribIPointer = void(APIENTRY*)(unsigned int, int, unsigned int, int, const void*);
using DrawArrays = void(APIENTRY*)(unsigned int, int, int);
using Capability = void(APIENTRY*)(unsigned int);

VertexAttribIPointer vertexAttribIPointer = nullptr;
DrawArrays drawArrays = nullptr;
Capability enable = nullptr;
Capability disable = nullptr;

template <typename Function>
bool loadExtension(const char* name, Function& target) {
    target = reinterpret_cast<Function>(reinterpret_cast<void*>(wglGetProcAddress(name)));
    return target != nullptr;
}

template <typename Function>
bool loadCore(HMODULE library, const char* name, Function& target) {
    target = reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(library, name)));
    return target != nullptr;
}

}

namespace glExtras {

bool load() {
    HMODULE openGl = GetModuleHandleA("opengl32.dll");
    return openGl && loadExtension("glVertexAttribIPointer", vertexAttribIPointer) &&
           loadCore(openGl, "glDrawArrays", drawArrays) && loadCore(openGl, "glEnable", enable) &&
           loadCore(openGl, "glDisable", disable);
}

void vertexAttribIntegerPointer(unsigned int index, int size, unsigned int type, int stride, const void* offset) {
    vertexAttribIPointer(index, size, type, stride, offset);
}

void drawPoints(int first, int count) { drawArrays(glPoints, first, count); }

void enableProgramPointSize(bool enabled) {
    (enabled ? enable : disable)(glProgramPointSize);
}

}
