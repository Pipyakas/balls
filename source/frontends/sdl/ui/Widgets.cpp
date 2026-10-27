#include "ui/Widgets.h"

#include "render/Palette.h"

#include <algorithm>
#include <string>

namespace {

constexpr float buttonTextPadding = 12.0f;

}

namespace ui {

Vec2 measureText(std::string_view text, float size) {
    return gfx::measureText(text, size);
}

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

void drawPanel(Rect rect, Color fill, float roundness) {
    gfx::fillRounded(rect, roundness * style::roundnessScale, fill);
    drawBorder(rect, roundness);
}

void drawBorder(Rect rect, float roundness, Color color) {
    gfx::strokeRounded(rect, roundness * style::roundnessScale, style::borderWidth, color);
}

// A widget scrolled out of a clipped gadget must not take clicks meant for whatever is drawn there instead.
bool isHovered(Rect rect) {
    const Vec2 mouse = input::mousePosition();
    return pointInRect(mouse, rect) && pointInRect(mouse, gfx::currentClip());
}

bool isClicked(Rect rect) {
    return isHovered(rect) && input::mousePressed(input::MouseButton::Left);
}

bool button(Rect rect, std::string_view label, bool enabled, bool selected) {
    const bool hovered = enabled && isHovered(rect);
    const bool pressed = hovered && input::mouseDown(input::MouseButton::Left);
    Color fill = palette::panelLight;
    if (!enabled) {
        fill = palette::panel;
    } else if (pressed) {
        fill = selected ? palette::shade(palette::accent, 0.85f) : palette::controlPressed;
    } else if (hovered) {
        fill = selected ? palette::shade(palette::accent, 1.1f) : palette::controlHover;
    } else if (selected) {
        fill = palette::accent;
    }
    const float roundness = style::buttonRoundness * style::roundnessScale;
    gfx::fillRounded(rect, roundness, fill);
    drawBorder(rect, style::buttonRoundness, selected ? palette::accent : palette::frame);
    const Color textColor = selected ? palette::background : (enabled ? palette::text : palette::textDim);
    const float preferredSize = std::min(rect.height * style::buttonLabelShare, style::buttonLabelMaximum);
    const float labelWidth = measureText(label, preferredSize).x;
    const float availableWidth = rect.width - buttonTextPadding;
    const float size =
        labelWidth > availableWidth ? preferredSize * availableWidth / labelWidth : preferredSize;
    drawTextInRect(label, rect, size, textColor);
    return hovered && input::mouseReleased(input::MouseButton::Left);
}

bool stepper(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step) {
    const float buttonWidth = rect.height;
    const float labelWidth = rect.width * 0.45f;
    drawTextInRect(label, {rect.x, rect.y, labelWidth, rect.height}, rect.height * 0.5f, palette::textMuted,
                   Align::Left);
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

bool dropdown(Rect rect, std::string_view current, bool& open) {
    constexpr float valueInset = 12.0f;
    constexpr float chevronHalfWidth = 6.0f;
    constexpr float chevronHalfHeight = 3.5f;
    const bool hovered = isHovered(rect);
    const Color fill = hovered && input::mouseDown(input::MouseButton::Left) ? palette::controlPressed
                       : hovered                                            ? palette::controlHover
                                                                            : palette::backgroundDeep;
    gfx::fillRounded(rect, style::buttonRoundness * style::roundnessScale, fill);
    drawBorder(rect, style::buttonRoundness, open || hovered ? palette::accent : palette::frame);

    const Rect handle{rect.x + rect.width - rect.height, rect.y, rect.height, rect.height};
    gfx::drawLine({handle.x, rect.y + 6.0f}, {handle.x, rect.y + rect.height - 6.0f}, 1.0f, palette::frame);
    const Vec2 center{handle.x + handle.width * 0.5f, handle.y + handle.height * 0.5f};
    const Color chevron = open || hovered ? palette::accent : palette::textMuted;
    const Vec2 left{center.x - chevronHalfWidth, center.y + (open ? chevronHalfHeight : -chevronHalfHeight)};
    const Vec2 right{center.x + chevronHalfWidth, left.y};
    const Vec2 tip{center.x, center.y + (open ? -chevronHalfHeight : chevronHalfHeight)};
    if (open) {
        gfx::fillTriangle(right, left, tip, chevron);
    } else {
        gfx::fillTriangle(left, right, tip, chevron);
    }

    const float size = std::min(rect.height * style::buttonLabelShare, style::buttonLabelMaximum);
    drawTextInRect(current, {rect.x + valueInset, rect.y, handle.x - rect.x - valueInset * 2.0f, rect.height}, size,
                   palette::text, Align::Left);
    if (hovered && input::mouseReleased(input::MouseButton::Left)) {
        open = !open;
    }
    return open;
}

void scrollFrame(Rect area, float& scroll, float contentHeight, float& visibleHeight) {
    constexpr float wheelStep = 40.0f;
    constexpr float barWidth = 4.0f;
    visibleHeight = area.height;
    const float overflow = contentHeight - area.height;
    if (overflow <= 0.0f) {
        scroll = 0.0f;
        return;
    }
    if (pointInRect(input::mousePosition(), area)) {
        scroll += input::wheelMove() * wheelStep * -1.0f;
    }
    scroll = std::clamp(scroll, 0.0f, overflow);
    const float barHeight = std::max(24.0f, area.height * area.height / contentHeight);
    const float barY = area.y + scroll / overflow * (area.height - barHeight);
    gfx::fillRect({area.x + area.width - barWidth, barY, barWidth, barHeight}, palette::frame);
}

bool textField(Rect rect, std::string& text, bool& focused, size_t maximumLength) {
    if (input::mousePressed(input::MouseButton::Left)) {
        focused = isHovered(rect);
    }
    bool changed = false;
    if (focused) {
        input::requestTextInput();
        for (int character = input::nextTypedCharacter(); character > 0;
             character = input::nextTypedCharacter()) {
            const bool allowed =
                (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') || character == '_' || character == '-';
            if (allowed && text.size() < maximumLength) {
                text.push_back(static_cast<char>(character));
                changed = true;
            }
        }
        if ((input::keyPressed(SDL_SCANCODE_BACKSPACE) || input::keyRepeated(SDL_SCANCODE_BACKSPACE)) &&
            !text.empty()) {
            text.pop_back();
            changed = true;
        }
        if (input::keyPressed(SDL_SCANCODE_RETURN)) {
            focused = false;
        }
    }
    gfx::fillRounded(rect, style::fieldRoundness * style::roundnessScale,
                     focused ? palette::controlHover : palette::panelLight);
    gfx::strokeRounded(rect, style::fieldRoundness * style::roundnessScale,
                       focused ? style::focusBorderWidth : style::borderWidth,
                       focused ? palette::accent : palette::frame);
    const bool caretVisible = focused && static_cast<int>(input::seconds() * 2.0) % 2 == 0;
    drawTextInRect(text + (caretVisible ? "|" : ""), shrink(rect, 10.0f), rect.height * 0.5f, palette::text,
                   Align::Left);
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

} // namespace ui
