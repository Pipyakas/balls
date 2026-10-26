#include "ui/Widgets.h"

#include "render/Palette.h"

#include <algorithm>
#include <string>

namespace {

constexpr int fontAtlasSize = 64;
constexpr float buttonTextPadding = 12.0f;
constexpr float textSpacing = 1.0f;
constexpr const char* preferredFontPath = "C:/Windows/Fonts/segoeuib.ttf";

Font loadedFont{};
bool hasCustomFont = false;

}

namespace ui {

void loadFonts() {
    if (FileExists(preferredFontPath)) {
        loadedFont = LoadFontEx(preferredFontPath, fontAtlasSize, nullptr, 0);
        hasCustomFont = loadedFont.texture.id != 0;
        if (hasCustomFont) {
            SetTextureFilter(loadedFont.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
    if (!hasCustomFont) {
        loadedFont = GetFontDefault();
    }
}

void unloadFonts() {
    if (hasCustomFont) {
        UnloadFont(loadedFont);
        hasCustomFont = false;
    }
}

const Font& font() { return loadedFont; }

Vector2 measureText(std::string_view text, float size) {
    const std::string owned(text);
    return MeasureTextEx(loadedFont, owned.c_str(), size, textSpacing);
}

void drawText(std::string_view text, Vector2 position, float size, Color color, Align align) {
    const std::string owned(text);
    const Vector2 extent = MeasureTextEx(loadedFont, owned.c_str(), size, textSpacing);
    if (align == Align::Center) {
        position.x -= extent.x * 0.5f;
    } else if (align == Align::Right) {
        position.x -= extent.x;
    }
    DrawTextEx(loadedFont, owned.c_str(), position, size, textSpacing, color);
}

void drawTextInRect(std::string_view text, Rectangle rect, float size, Color color, Align align) {
    const Vector2 extent = measureText(text, size);
    const float y = rect.y + (rect.height - extent.y) * 0.5f;
    float x = rect.x + rect.width * 0.5f;
    if (align == Align::Left) {
        x = rect.x;
    } else if (align == Align::Right) {
        x = rect.x + rect.width;
    }
    drawText(text, {x, y}, size, color, align);
}

void drawPanel(Rectangle rect, Color fill, float roundness) {
    DrawRectangleRounded(rect, roundness, 8, fill);
}

bool isHovered(Rectangle rect) { return CheckCollisionPointRec(GetMousePosition(), rect); }

bool isClicked(Rectangle rect) { return isHovered(rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

bool button(Rectangle rect, std::string_view label, bool enabled, bool selected) {
    const bool hovered = enabled && isHovered(rect);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color fill = selected ? palette::accent : palette::panelLight;
    if (!enabled) {
        fill = palette::shade(palette::panel, 0.8f);
    } else if (pressed) {
        fill = palette::shade(fill, 0.8f);
    } else if (hovered) {
        fill = palette::shade(fill, 1.2f);
    }
    const Rectangle shadow{rect.x, rect.y + 4.0f, rect.width, rect.height};
    DrawRectangleRounded(shadow, 0.3f, 8, palette::shade(fill, 0.55f));
    DrawRectangleRounded(pressed ? shadow : rect, 0.3f, 8, fill);
    const Color textColor = selected ? palette::backgroundDeep : (enabled ? palette::text : palette::textMuted);
    const float preferredSize = std::min(rect.height * 0.5f, 30.0f);
    const float labelWidth = measureText(label, preferredSize).x;
    const float availableWidth = rect.width - buttonTextPadding;
    const float size = labelWidth > availableWidth ? preferredSize * availableWidth / labelWidth : preferredSize;
    drawTextInRect(label, pressed ? shadow : rect, size, textColor);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool stepper(Rectangle rect, std::string_view label, int& value, int minimum, int maximum, int step) {
    const float buttonWidth = rect.height;
    const float labelWidth = rect.width * 0.45f;
    drawTextInRect(label, {rect.x, rect.y, labelWidth, rect.height}, rect.height * 0.5f, palette::textMuted, Align::Left);
    const Rectangle minus{rect.x + labelWidth, rect.y, buttonWidth, rect.height};
    const Rectangle plus{rect.x + rect.width - buttonWidth, rect.y, buttonWidth, rect.height};
    const Rectangle valueRect{minus.x + buttonWidth, rect.y, plus.x - minus.x - buttonWidth, rect.height};
    const int fastStep = IsKeyDown(KEY_LEFT_SHIFT) ? step * 10 : step;
    const int before = value;
    if (button(minus, "-", value > minimum)) {
        value = std::max(minimum, value - fastStep);
    }
    if (button(plus, "+", value < maximum)) {
        value = std::min(maximum, value + fastStep);
    }
    drawTextInRect(std::to_string(value), valueRect, rect.height * 0.55f, palette::text);
    return value != before;
}

bool textField(Rectangle rect, std::string& text, bool& focused, size_t maximumLength) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        focused = isHovered(rect);
    }
    bool changed = false;
    if (focused) {
        for (int character = GetCharPressed(); character > 0; character = GetCharPressed()) {
            const bool allowed = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                                 (character >= 'A' && character <= 'Z') || character == '_' || character == '-';
            if (allowed && text.size() < maximumLength) {
                text.push_back(static_cast<char>(character));
                changed = true;
            }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            text.pop_back();
            changed = true;
        }
        if (IsKeyPressed(KEY_ENTER)) {
            focused = false;
        }
    }
    DrawRectangleRounded(rect, 0.2f, 8, focused ? palette::panelLight : palette::backgroundDeep);
    DrawRectangleRoundedLinesEx(rect, 0.2f, 8, 2.0f, focused ? palette::accent : palette::frame);
    const bool caretVisible = focused && static_cast<int>(GetTime() * 2.0) % 2 == 0;
    drawTextInRect(text + (caretVisible ? "|" : ""), shrink(rect, 10.0f), rect.height * 0.5f, palette::text, Align::Left);
    return changed;
}

Rectangle shrink(Rectangle rect, float amount) {
    return {rect.x + amount, rect.y + amount, rect.width - amount * 2.0f, rect.height - amount * 2.0f};
}

Rectangle rowSlice(Rectangle rect, int index, int count, float gap) {
    const float height = (rect.height - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
    return {rect.x, rect.y + static_cast<float>(index) * (height + gap), rect.width, height};
}

Rectangle columnSlice(Rectangle rect, int index, int count, float gap) {
    const float width = (rect.width - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
    return {rect.x + static_cast<float>(index) * (width + gap), rect.y, width, rect.height};
}

}
