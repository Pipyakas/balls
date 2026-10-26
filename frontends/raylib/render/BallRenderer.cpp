#include "render/BallRenderer.h"

#include "render/GlExtras.h"
#include "render/Palette.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>

namespace {

constexpr unsigned int packedLocation = 0;
constexpr size_t initialCapacity = 1 << 16;

constexpr const char* vertexShaderSource = R"(#version 330
layout(location = 0) in uint packedBall;
uniform mat4 mvp;
uniform vec2 boardOrigin;
uniform float cellPixels;
uniform float radius;
uniform vec2 unpackScale;
uniform vec3 palette[7];
flat out vec3 tint;
void main() {
    vec2 cell = vec2(float(packedBall & 16383u), float((packedBall >> 14) & 16383u)) * unpackScale;
    tint = palette[min(packedBall >> 28, 6u)];
    gl_Position = mvp * vec4(boardOrigin + cell * cellPixels, 0.0, 1.0);
    gl_PointSize = max(2.0 * radius * cellPixels, 1.0);
}
)";

constexpr const char* fragmentShaderSource = R"(#version 330
flat in vec3 tint;
out vec4 finalColor;
void main() {
    vec2 local = gl_PointCoord * 2.0 - 1.0;
    float distanceSquared = dot(local, local);
    if (distanceSquared > 1.0) {
        discard;
    }
    float highlight = clamp(1.0 - length(local - vec2(-0.35, -0.4)) * 1.6, 0.0, 1.0);
    finalColor = vec4(tint * (0.65 + 0.35 * (1.0 - distanceSquared)) + vec3(highlight * highlight * 0.55), 1.0);
}
)";

}

void BallRenderer::load() {
    glExtras::load();
    shader = LoadShaderFromMemory(vertexShaderSource, fragmentShaderSource);
    mvpLocation = GetShaderLocation(shader, "mvp");
    originLocation = GetShaderLocation(shader, "boardOrigin");
    cellPixelsLocation = GetShaderLocation(shader, "cellPixels");
    radiusLocation = GetShaderLocation(shader, "radius");
    unpackScaleLocation = GetShaderLocation(shader, "unpackScale");
    paletteLocation = GetShaderLocation(shader, "palette");

    std::array<float, ballColorCount * 3> colors{};
    for (int i = 0; i < ballColorCount; ++i) {
        const Color color = palette::ballColor(i);
        colors[i * 3 + 0] = color.r / 255.0f;
        colors[i * 3 + 1] = color.g / 255.0f;
        colors[i * 3 + 2] = color.b / 255.0f;
    }
    SetShaderValueV(shader, paletteLocation, colors.data(), SHADER_UNIFORM_VEC3, ballColorCount);
    vertexArray = rlLoadVertexArray();
    ensureCapacity(initialCapacity);
}

void BallRenderer::unload() {
    if (packedBuffer) {
        rlUnloadVertexBuffer(packedBuffer);
    }
    if (vertexArray) {
        rlUnloadVertexArray(vertexArray);
    }
    UnloadShader(shader);
    capacity = 0;
}

bool BallRenderer::ensureCapacity(size_t ballCount) {
    if (ballCount <= capacity) {
        return false;
    }
    capacity = std::max(ballCount, capacity * 2);
    rlEnableVertexArray(vertexArray);
    if (packedBuffer) {
        rlUnloadVertexBuffer(packedBuffer);
    }
    packedBuffer = rlLoadVertexBuffer(nullptr, static_cast<int>(capacity * sizeof(uint32_t)), true);
    glExtras::vertexAttribIntegerPointer(packedLocation, 1, glExtras::unsignedIntType, 0, nullptr);
    rlEnableVertexAttribute(packedLocation);
    rlDisableVertexArray();
    return true;
}

double BallRenderer::takeSynchronizationMs() {
    const double taken = synchronizationMs;
    synchronizationMs = 0.0;
    return taken;
}

void BallRenderer::shareWith(PhysicsBackend& backend) {
    sharedBackend = &backend;
    sharedBufferAccepted = backend.shareRenderBuffer(packedBuffer, capacity * sizeof(uint32_t));
}

void BallRenderer::draw(PhysicsBackend& backend, const BoardTransform& transform, float radiusCells) {
    if (backend.ballCount() == 0) {
        return;
    }
    rlDrawRenderBatchActive();
    if (ensureCapacity(backend.ballCount()) || sharedBackend != &backend) {
        shareWith(backend);
    }
    const auto snapshotStart = std::chrono::steady_clock::now();
    const BallSnapshot balls = backend.snapshot();
    if (balls.residentInRenderBuffer) {
        synchronizationMs +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - snapshotStart).count();
    }
    if (balls.count == 0) {
        return;
    }
    if (!balls.residentInRenderBuffer) {
        if (!balls.packed) {
            return;
        }
        rlUpdateVertexBuffer(packedBuffer, balls.packed, static_cast<int>(balls.count * sizeof(uint32_t)), 0);
    }

    const Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    const float origin[2]{transform.origin.x, transform.origin.y};
    const float unpackScale[2]{1.0f / renderPackScale(balls.fieldWidth), 1.0f / renderPackScale(balls.fieldHeight)};
    rlEnableShader(shader.id);
    rlSetUniformMatrix(mvpLocation, mvp);
    rlSetUniform(originLocation, origin, SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(cellPixelsLocation, &transform.cellPixels, SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(radiusLocation, &radiusCells, SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(unpackScaleLocation, unpackScale, SHADER_UNIFORM_VEC2, 1);
    rlDisableColorBlend();
    glExtras::enableProgramPointSize(true);
    rlEnableVertexArray(vertexArray);
    glExtras::drawPoints(0, static_cast<int>(balls.count));
    rlDisableVertexArray();
    glExtras::enableProgramPointSize(false);
    rlEnableColorBlend();
    rlDisableShader();
}
