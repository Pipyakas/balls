#pragma once

#include "ui/Geometry.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

namespace input {

enum class MouseButton { Left, Right, Middle };

void clearTransientState();
void handleEvent(const SDL_Event& event);

bool keyPressed(SDL_Scancode key);
bool keyRepeated(SDL_Scancode key);
bool keyDown(SDL_Scancode key);
bool mousePressed(MouseButton button);
bool mouseReleased(MouseButton button);
bool mouseDown(MouseButton button);
Vec2 mousePosition();
float wheelMove();
int nextTypedCharacter();
double seconds();

void requestTextInput();
bool wantsTextInput();

}
