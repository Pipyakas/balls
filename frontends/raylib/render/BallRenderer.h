#pragma once

#include "render/ScreenLayout.h"
#include "sim/PhysicsBackend.h"

#include <raylib.h>

#include <chrono>

struct BallShader {
    Shader shader{};
    int mvp = -1;
    int origin = -1;
    int cellPixels = -1;
    int radius = -1;
    int unpackScale = -1;
    int depthPerBall = -1;
};

class BallRenderer {
public:
    void load();
    void unload();
    void draw(PhysicsBackend& backend, const BoardTransform& transform, float radiusCells);
    double takeSynchronizationMs();

private:
    bool ensureCapacity(size_t ballCount);
    void shareWith(PhysicsBackend& backend);

    BallShader sparseShader;
    BallShader denseShader;
    bool denseUsesDepth = true;
    unsigned int vertexArray = 0;
    unsigned int packedBuffer = 0;
    size_t capacity = 0;
    const PhysicsBackend* sharedBackend = nullptr;
    bool sharedBufferAccepted = false;
    double synchronizationMs = 0.0;
};
