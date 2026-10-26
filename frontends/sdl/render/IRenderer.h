#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct SDL_Window;

struct RendererRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
    float cornerRadius = 0.0f;
    float bevelHeight = 0.0f;
    float highlightHeight = 0.0f;
    float unused = 0.0f;
};

inline constexpr int rendererPaletteSize = 7;

struct BallDrawRequest {
    const uint32_t* packed = nullptr;
    size_t count = 0;
    bool isVisibilityGrid = false;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    float originX = 0.0f;
    float originY = 0.0f;
    float cellPixels = 1.0f;
    float radiusCells = 0.2f;
    float slotsPerRadius = 1.6f;
    float clipX = 0.0f;
    float clipY = 0.0f;
    float clipWidth = 0.0f;
    float clipHeight = 0.0f;
    std::array<std::array<float, 4>, rendererPaletteSize> palette{};
};

struct ClearColor {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool beginFrame() = 0;
    virtual void drawRects(std::span<const RendererRect> rects) = 0;
    virtual void drawBalls(const BallDrawRequest& request) = 0;
    virtual void endFrame(ClearColor clearColor) = 0;

    virtual float viewWidth() const = 0;
    virtual float viewHeight() const = 0;
    virtual bool setVsync(bool enabled) = 0;
    virtual bool isVsyncEnabled() const = 0;
    virtual void requestScreenshot(const std::string& path) = 0;
    virtual bool lastScreenshotSucceeded() const = 0;
    virtual double lastGpuWaitMs() const = 0;
    virtual double lastGpuBusyMs() const = 0;
    virtual std::string deviceDescription() const = 0;
};

std::unique_ptr<IRenderer> createSdlGpuRenderer(SDL_Window* window, std::string& error);
