#include "ui/Painter.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace {

struct PainterState {
    float viewWidth = 1.0f;
    float viewHeight = 1.0f;
    ImFont* font = nullptr;
    std::vector<RendererRect> scene;
    std::vector<Rect> clips;
};

PainterState state;

ImU32 packColor(Color color) { return IM_COL32(color.r, color.g, color.b, color.a); }

ImDrawList& layer() { return *ImGui::GetBackgroundDrawList(); }

float cornerRadius(Rect rect, float roundness) {
    return std::clamp(roundness, 0.0f, 1.0f) * std::min(rect.width, rect.height) * 0.5f;
}

}

namespace gfx {

void beginFrame(float viewWidth, float viewHeight, ImFont* font) {
    state.viewWidth = viewWidth;
    state.viewHeight = viewHeight;
    state.font = font;
    state.scene.clear();
    state.clips.clear();
}

std::span<const RendererRect> sceneRects() { return state.scene; }

int screenWidth() { return static_cast<int>(state.viewWidth); }

int screenHeight() { return static_cast<int>(state.viewHeight); }

Color fade(Color color, float alpha) {
    color.a = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return color;
}

void sceneRect(Rect rect, Color color, float radius) {
    constexpr float channelScale = 1.0f / 255.0f;
    const Rect clip = currentClip();
    const float left = std::max(rect.x, clip.x);
    const float top = std::max(rect.y, clip.y);
    const float right = std::min(rect.x + rect.width, clip.x + clip.width);
    const float bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    if (right <= left || bottom <= top) {
        return;
    }
    rect = {left, top, right - left, bottom - top};
    RendererRect instance;
    instance.x = rect.x;
    instance.y = rect.y;
    instance.width = rect.width;
    instance.height = rect.height;
    instance.red = color.r * channelScale;
    instance.green = color.g * channelScale;
    instance.blue = color.b * channelScale;
    instance.alpha = color.a * channelScale;
    instance.cornerRadius = radius;
    state.scene.push_back(instance);
}

void fillRect(Rect rect, Color color) {
    layer().AddRectFilled({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, packColor(color));
}

void fillRectAt(Vec2 position, Vec2 size, Color color) { fillRect({position.x, position.y, size.x, size.y}, color); }

void fillRounded(Rect rect, float roundness, Color color) {
    layer().AddRectFilled({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, packColor(color),
                          cornerRadius(rect, roundness));
}

void strokeRounded(Rect rect, float roundness, float thickness, Color color) {
    const float half = thickness * 0.5f;
    layer().AddRect({rect.x + half, rect.y + half}, {rect.x + rect.width - half, rect.y + rect.height - half},
                    packColor(color), cornerRadius(rect, roundness), 0, thickness);
}

void strokeRect(Rect rect, float thickness, Color color) { strokeRounded(rect, 0.0f, thickness, color); }

void fillCircle(Vec2 center, float radius, Color color) {
    layer().AddCircleFilled({center.x, center.y}, radius, packColor(color));
}

void fillTriangle(Vec2 first, Vec2 second, Vec2 third, Color color) {
    layer().AddTriangleFilled({first.x, first.y}, {second.x, second.y}, {third.x, third.y}, packColor(color));
}

void drawLine(Vec2 start, Vec2 end, float thickness, Color color) {
    layer().AddLine({start.x, start.y}, {end.x, end.y}, packColor(color), thickness);
}

void beginClip(Rect rect) {
    const Rect outer = currentClip();
    const float left = std::max(rect.x, outer.x);
    const float top = std::max(rect.y, outer.y);
    const float right = std::max(left, std::min(rect.x + rect.width, outer.x + outer.width));
    const float bottom = std::max(top, std::min(rect.y + rect.height, outer.y + outer.height));
    state.clips.push_back({left, top, right - left, bottom - top});
    layer().PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
}

void endClip() {
    if (!state.clips.empty()) {
        state.clips.pop_back();
    }
    layer().PopClipRect();
}

Rect currentClip() { return state.clips.empty() ? Rect{0.0f, 0.0f, state.viewWidth, state.viewHeight} : state.clips.back(); }

Vec2 measureText(std::string_view text, float size) {
    if (!state.font || text.empty()) {
        return {0.0f, size};
    }
    const ImVec2 extent = state.font->CalcTextSizeA(size, 1.0e9f, 0.0f, text.data(), text.data() + text.size());
    return {extent.x, size};
}

void drawText(std::string_view text, Vec2 position, float size, Color color) {
    if (!state.font || text.empty()) {
        return;
    }
    layer().AddText(state.font, size, {position.x, position.y}, packColor(color), text.data(), text.data() + text.size());
}

}
