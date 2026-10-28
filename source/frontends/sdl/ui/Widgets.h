#pragma once

#include "app/Input.h"
#include "render/Palette.h"
#include "ui/Painter.h"

#include <string>
#include <string_view>

namespace ui {

enum class Align { Left, Center, Right };

namespace style {

inline constexpr float roundnessScale = 0.0f;
inline constexpr float borderWidth = 1.0f;
inline constexpr float focusBorderWidth = 2.0f;
inline constexpr float buttonRoundness = 0.3f;
inline constexpr float fieldRoundness = 0.2f;
inline constexpr float buttonLabelShare = 0.45f;
inline constexpr float buttonLabelMaximum = 26.0f;

} // namespace style

Vec2 measureText(std::string_view text, float size);
void drawText(std::string_view text, Vec2 position, float size, Color color, Align align = Align::Left);
void drawTextInRect(std::string_view text, Rect rect, float size, Color color, Align align = Align::Center);
// Left-aligned text that keeps its size: when wider than the rect it drifts left to show its end and back,
// resting at each end, clipped to the rect.
void drawScrollingText(std::string_view text, Rect rect, float size, Color color);

void drawPanel(Rect rect, Color fill, float roundness = 0.08f);
void drawBorder(Rect rect, float roundness = 0.0f, Color color = palette::frame);
bool button(Rect rect, std::string_view label, bool enabled = true, bool selected = false);
bool stepper(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step = 1);
bool dropdown(Rect rect, std::string_view current, bool& open);
void scrollFrame(Rect area, float& scroll, float contentHeight, float& visibleHeight);
// Instead of scrollbars, an edge with more content beyond it fades into the gadget's background under a faded
// arrow pointing that way. Draw them after the content, inside its clip.
void scrollHints(Rect area, float scroll, float contentHeight, Color background);
void horizontalScrollHints(Rect area, float scroll, float contentWidth, Color background);
bool textField(Rect rect, std::string& text, bool& focused, size_t maximumLength);
bool isHovered(Rect rect);
bool isClicked(Rect rect);

Rect shrink(Rect rect, float amount);
Rect rowSlice(Rect rect, int index, int count, float gap);
Rect columnSlice(Rect rect, int index, int count, float gap);

} // namespace ui
