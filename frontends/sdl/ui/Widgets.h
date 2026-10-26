#pragma once

#include "app/Input.h"
#include "ui/Painter.h"

#include <string>
#include <string_view>

namespace ui {

enum class Align { Left, Center, Right };

Vec2 measureText(std::string_view text, float size);
void drawText(std::string_view text, Vec2 position, float size, Color color, Align align = Align::Left);
void drawTextInRect(std::string_view text, Rect rect, float size, Color color, Align align = Align::Center);

void drawPanel(Rect rect, Color fill, float roundness = 0.08f);
bool button(Rect rect, std::string_view label, bool enabled = true, bool selected = false);
bool stepper(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step = 1);
bool textField(Rect rect, std::string& text, bool& focused, size_t maximumLength);
bool isHovered(Rect rect);
bool isClicked(Rect rect);

Rect shrink(Rect rect, float amount);
Rect rowSlice(Rect rect, int index, int count, float gap);
Rect columnSlice(Rect rect, int index, int count, float gap);

}
