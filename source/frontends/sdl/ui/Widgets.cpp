#include "ui/Widgets.h"

#include "render/Palette.h"

#include <algorithm>
#include <string>

namespace {

constexpr float buttonTextPadding = 12.0f;

}

namespace ui {

Vec2 measureText(std::string_view text, float size) { return gfx::measureText(text, size); }

void drawText(std::string_view text, Vec2 position, float size, Color color, Align align) {
    const Vec2 extent = gfx::measureText(text, size);
    if (align == Align::Center) {
        position.x -= extent.x * 0.5f;
    } else if (align == Align::Right) {
        position.x -= extent.x;
    }
    gfx::drawText(text, position, size, color);
}

void drawTextInRect(std::string_view text, Rect rect, float size, Color color, Align align) {
    const Vec2 extent = measureText(text, size);
    const float y = rect.y + (rect.height - extent.y) * 0.5f;
    float x = rect.x + rect.width * 0.5f;
    if (align == Align::Left) {
        x = rect.x;
    } else if (align == Align::Right) {
        x = rect.x + rect.width;
    }
    drawText(text, {x, y}, size, color, align);
}

void drawPanel(Rect rect, Color fill, float roundness) { gfx::fillRounded(rect, roundness, fill); }

bool isHovered(Rect rect) { return pointInRect(input::mousePosition(), rect); }

bool isClicked(Rect rect) { return isHovered(rect) && input::mousePressed(input::MouseButton::Left); }

bool button(Rect rect, std::string_view label, bool enabled, bool selected) {
    const bool hovered = enabled && isHovered(rect);
    const bool pressed = hovered && input::mouseDown(input::MouseButton::Left);
    Color fill = selected ? palette::accent : palette::panelLight;
    if (!enabled) {
        fill = palette::shade(palette::panel, 0.8f);
    } else if (pressed) {
        fill = palette::shade(fill, 0.8f);
    } else if (hovered) {
        fill = palette::shade(fill, 1.2f);
    }
    const Rect shadow{rect.x, rect.y + 4.0f, rect.width, rect.height};
    gfx::fillRounded(shadow, 0.3f, palette::shade(fill, 0.55f));
    gfx::fillRounded(pressed ? shadow : rect, 0.3f, fill);
    const Color textColor = selected ? palette::backgroundDeep : (enabled ? palette::text : palette::textMuted);
    const float preferredSize = std::min(rect.height * 0.5f, 30.0f);
    const float labelWidth = measureText(label, preferredSize).x;
    const float availableWidth = rect.width - buttonTextPadding;
    const float size = labelWidth > availableWidth ? preferredSize * availableWidth / labelWidth : preferredSize;
    drawTextInRect(label, pressed ? shadow : rect, size, textColor);
    return hovered && input::mouseReleased(input::MouseButton::Left);
}

bool stepper(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step) {
    const float buttonWidth = rect.height;
    const float labelWidth = rect.width * 0.45f;
    drawTextInRect(label, {rect.x, rect.y, labelWidth, rect.height}, rect.height * 0.5f, palette::textMuted, Align::Left);
    const Rect minus{rect.x + labelWidth, rect.y, buttonWidth, rect.height};
    const Rect plus{rect.x + rect.width - buttonWidth, rect.y, buttonWidth, rect.height};
    const Rect valueRect{minus.x + buttonWidth, rect.y, plus.x - minus.x - buttonWidth, rect.height};
    const int fastStep = input::keyDown(SDL_SCANCODE_LSHIFT) ? step * 10 : step;
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

bool textField(Rect rect, std::string& text, bool& focused, size_t maximumLength) {
    if (input::mousePressed(input::MouseButton::Left)) {
        focused = isHovered(rect);
    }
    bool changed = false;
    if (focused) {
        input::requestTextInput();
        for (int character = input::nextTypedCharacter(); character > 0; character = input::nextTypedCharacter()) {
            const bool allowed = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                                 (character >= 'A' && character <= 'Z') || character == '_' || character == '-';
            if (allowed && text.size() < maximumLength) {
                text.push_back(static_cast<char>(character));
                changed = true;
            }
        }
        if ((input::keyPressed(SDL_SCANCODE_BACKSPACE) || input::keyRepeated(SDL_SCANCODE_BACKSPACE)) && !text.empty()) {
            text.pop_back();
            changed = true;
        }
        if (input::keyPressed(SDL_SCANCODE_RETURN)) {
            focused = false;
        }
    }
    gfx::fillRounded(rect, 0.2f, focused ? palette::panelLight : palette::backgroundDeep);
    gfx::strokeRounded(rect, 0.2f, 2.0f, focused ? palette::accent : palette::frame);
    const bool caretVisible = focused && static_cast<int>(input::seconds() * 2.0) % 2 == 0;
    drawTextInRect(text + (caretVisible ? "|" : ""), shrink(rect, 10.0f), rect.height * 0.5f, palette::text, Align::Left);
    return changed;
}

Rect shrink(Rect rect, float amount) {
    return {rect.x + amount, rect.y + amount, rect.width - amount * 2.0f, rect.height - amount * 2.0f};
}

Rect rowSlice(Rect rect, int index, int count, float gap) {
    const float height = (rect.height - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
    return {rect.x, rect.y + static_cast<float>(index) * (height + gap), rect.width, height};
}

Rect columnSlice(Rect rect, int index, int count, float gap) {
    const float width = (rect.width - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
    return {rect.x + static_cast<float>(index) * (width + gap), rect.y, width, rect.height};
}

}
