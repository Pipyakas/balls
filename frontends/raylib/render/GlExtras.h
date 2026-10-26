#pragma once

namespace glExtras {

bool load();
void vertexAttribIntegerPointer(unsigned int index, int size, unsigned int type, int stride, const void* offset);
void drawPoints(int first, int count);
void enableProgramPointSize(bool enabled);

inline constexpr unsigned int unsignedIntType = 0x1405;

}
