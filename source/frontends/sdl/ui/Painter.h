#pragma once

#include "render/IRenderer.h"
#include "ui/Geometry.h"

#include <span>
#include <string_view>

struct ImFont;

namespace gfx {

struct FontSet {
    ImFont* regular = nullptr;
    ImFont* strong = nullptr;
    float strongMinimumSize = 0.0f;
};

void beginFrame(float viewWidth, float viewHeight, FontSet fonts);
std::span<const RendererRect> sceneRects();

int screenWidth();
int screenHeight();

Color fade(Color color, float alpha);

void sceneRect(Rect rect, Color color, float cornerRadius = 0.0f);
void fillRect(Rect rect, Color color);
void fillRectAt(Vec2 position, Vec2 size, Color color);
void fillRounded(Rect rect, float roundness, Color color);
void strokeRounded(Rect rect, float roundness, float thickness, Color color);
void strokeRect(Rect rect, float thickness, Color color);
void fillCircle(Vec2 center, float radius, Color color);
void fillTriangle(Vec2 first, Vec2 second, Vec2 third, Color color);
void fillGradient(Rect rect, Color start, Color end, bool horizontal);
void drawLine(Vec2 start, Vec2 end, float thickness, Color color);
void beginClip(Rect rect);
Rect currentClip();
void endClip();

Vec2 measureText(std::string_view text, float size);
void drawText(std::string_view text, Vec2 position, float size, Color color);

}
