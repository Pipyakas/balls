#pragma once

#include <raylib.h>

#include <string>
#include <string_view>

namespace ui {

enum class Align { Left, Center, Right };

void loadFonts();
void unloadFonts();
const Font& font();

Vector2 measureText(std::string_view text, float size);
void drawText(std::string_view text, Vector2 position, float size, Color color, Align align = Align::Left);
void drawTextInRect(std::string_view text, Rectangle rect, float size, Color color, Align align = Align::Center);

void drawPanel(Rectangle rect, Color fill, float roundness = 0.08f);
bool button(Rectangle rect, std::string_view label, bool enabled = true, bool selected = false);
bool stepper(Rectangle rect, std::string_view label, int& value, int minimum, int maximum, int step = 1);
bool textField(Rectangle rect, std::string& text, bool& focused, size_t maximumLength);
bool isHovered(Rectangle rect);
bool isClicked(Rectangle rect);

Rectangle shrink(Rectangle rect, float amount);
Rectangle rowSlice(Rectangle rect, int index, int count, float gap);
Rectangle columnSlice(Rectangle rect, int index, int count, float gap);

}
