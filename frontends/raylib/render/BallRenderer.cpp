#include "render/BallRenderer.h"

#include "render/Palette.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>

namespace {

constexpr unsigned int cornerLocation = 0;
constexpr unsigned int ballXLocation = 1;
constexpr unsigned int ballYLocation = 2;
constexpr unsigned int ballColorLocation = 3;
constexpr int glFloat = 0x1406;
constexpr int glInt = 0x1404;
constexpr size_t initialCapacity = 1 << 14;
constexpr int quadVertexCount = 6;

constexpr const char* vertexShaderSource = R"(#version 330
layout(location = 0) in vec2 corner;
layout(location = 1) in float ballX;
layout(location = 2) in float ballY;
layout(location = 3) in float ballColor;
uniform mat4 mvp;
uniform vec2 boardOrigin;
uniform float cellPixels;
uniform float radius;
uniform vec3 palette[7];
out vec2 local;
flat out vec3 tint;
void main() {
    local = corner;
    tint = palette[clamp(int(ballColor + 0.5), 0, 6)];
    vec2 position = boardOrigin + (vec2(ballX, ballY) + corner * radius) * cellPixels;
    gl_Position = mvp * vec4(position, 0.0, 1.0);
}
)";

constexpr const char* fragmentShaderSource = R"(#version 330
in vec2 local;
flat in vec3 tint;
out vec4 finalColor;
void main() {
    float distanceFromCenter = length(local);
    float edgeWidth = max(fwidth(distanceFromCenter), 0.02);
    float coverage = 1.0 - smoothstep(1.0 - edgeWidth, 1.0, distanceFromCenter);
    if (coverage <= 0.0) {
        discard;
    }
    float highlight = clamp(1.0 - length(local - vec2(-0.35, -0.4)) * 1.6, 0.0, 1.0);
    vec3 shaded = tint * (0.65 + 0.35 * (1.0 - distanceFromCenter * distanceFromCenter)) + vec3(highlight * highlight * 0.55);
    finalColor = vec4(shaded, coverage);
}
)";

}

void BallRenderer::load() {
    shader = LoadShaderFromMemory(vertexShaderSource, fragmentShaderSource);
    mvpLocation = GetShaderLocation(shader, "mvp");
    originLocation = GetShaderLocation(shader, "boardOrigin");
    cellPixelsLocation = GetShaderLocation(shader, "cellPixels");
    radiusLocation = GetShaderLocation(shader, "radius");
    paletteLocation = GetShaderLocation(shader, "palette");

    std::array<float, ballColorCount * 3> colors{};
    for (int i = 0; i < ballColorCount; ++i) {
        const Color color = palette::ballColor(i);
        colors[i * 3 + 0] = color.r / 255.0f;
        colors[i * 3 + 1] = color.g / 255.0f;
        colors[i * 3 + 2] = color.b / 255.0f;
    }
    SetShaderValueV(shader, paletteLocation, colors.data(), SHADER_UNIFORM_VEC3, ballColorCount);

    constexpr std::array<float, quadVertexCount * 2> corners{-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertexArray = rlLoadVertexArray();
    rlEnableVertexArray(vertexArray);
    cornerBuffer = rlLoadVertexBuffer(corners.data(), static_cast<int>(sizeof(corners)), false);
    rlSetVertexAttribute(cornerLocation, 2, glFloat, false, 0, 0);
    rlEnableVertexAttribute(cornerLocation);
    rlDisableVertexArray();
    ensureCapacity(initialCapacity);
}

void BallRenderer::unload() {
    for (unsigned int buffer : {cornerBuffer, xBuffer, yBuffer, colorBuffer}) {
        if (buffer) {
            rlUnloadVertexBuffer(buffer);
        }
    }
    if (vertexArray) {
        rlUnloadVertexArray(vertexArray);
    }
    UnloadShader(shader);
    capacity = 0;
}

void BallRenderer::bindInstanceAttribute(unsigned int location, unsigned int buffer, int glType) {
    rlEnableVertexBuffer(buffer);
    rlSetVertexAttribute(location, 1, glType, false, 0, 0);
    rlSetVertexAttributeDivisor(location, 1);
    rlEnableVertexAttribute(location);
}

void BallRenderer::ensureCapacity(size_t ballCount) {
    if (ballCount <= capacity) {
        return;
    }
    capacity = std::max(ballCount, capacity * 2);
    const int bytes = static_cast<int>(capacity * sizeof(float));
    rlEnableVertexArray(vertexArray);
    for (unsigned int* buffer : {&xBuffer, &yBuffer, &colorBuffer}) {
        if (*buffer) {
            rlUnloadVertexBuffer(*buffer);
        }
        *buffer = rlLoadVertexBuffer(nullptr, bytes, true);
    }
    bindInstanceAttribute(ballXLocation, xBuffer, glFloat);
    bindInstanceAttribute(ballYLocation, yBuffer, glFloat);
    bindInstanceAttribute(ballColorLocation, colorBuffer, glInt);
    rlDisableVertexArray();
}

void BallRenderer::draw(const BallSnapshot& balls, const BoardTransform& transform, float radiusCells) {
    if (balls.count == 0) {
        return;
    }
    rlDrawRenderBatchActive();
    ensureCapacity(balls.count);
    const int bytes = static_cast<int>(balls.count * sizeof(float));
    rlUpdateVertexBuffer(xBuffer, balls.x, bytes, 0);
    rlUpdateVertexBuffer(yBuffer, balls.y, bytes, 0);
    rlUpdateVertexBuffer(colorBuffer, balls.color, bytes, 0);

    const Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlEnableShader(shader.id);
    rlSetUniformMatrix(mvpLocation, mvp);
    const float origin[2]{transform.origin.x, transform.origin.y};
    rlSetUniform(originLocation, origin, SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(cellPixelsLocation, &transform.cellPixels, SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(radiusLocation, &radiusCells, SHADER_UNIFORM_FLOAT, 1);
    rlDisableBackfaceCulling();
    rlEnableVertexArray(vertexArray);
    rlDrawVertexArrayInstanced(0, quadVertexCount, static_cast<int>(balls.count));
    rlDisableVertexArray();
    rlEnableBackfaceCulling();
    rlDisableShader();
}
