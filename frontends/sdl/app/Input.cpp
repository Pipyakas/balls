#include "app/Input.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <bitset>
#include <deque>

namespace {

constexpr size_t mouseButtonCount = 3;

struct InputState {
    std::bitset<SDL_SCANCODE_COUNT> down;
    std::bitset<SDL_SCANCODE_COUNT> pressed;
    std::bitset<SDL_SCANCODE_COUNT> repeated;
    std::array<bool, mouseButtonCount> buttonDown{};
    std::array<bool, mouseButtonCount> buttonPressed{};
    std::array<bool, mouseButtonCount> buttonReleased{};
    Vec2 mouse{};
    float wheel = 0.0f;
    std::deque<int> typed;
    bool textRequested = false;
};

InputState state;

int buttonSlot(Uint8 sdlButton) {
    switch (sdlButton) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return -1;
    }
}

void appendUtf8Codepoints(const char* text) {
    while (*text) {
        const Uint32 codepoint = SDL_StepUTF8(&text, nullptr);
        if (codepoint == 0) {
            break;
        }
        state.typed.push_back(static_cast<int>(codepoint));
    }
}

}

namespace input {

void clearTransientState() {
    state.pressed.reset();
    state.repeated.reset();
    state.buttonPressed.fill(false);
    state.buttonReleased.fill(false);
    state.wheel = 0.0f;
    state.typed.clear();
    state.textRequested = false;
}

void handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode < SDL_SCANCODE_COUNT) {
            if (event.key.repeat) {
                state.repeated.set(event.key.scancode);
            } else {
                state.pressed.set(event.key.scancode);
            }
            state.down.set(event.key.scancode);
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode < SDL_SCANCODE_COUNT) {
            state.down.reset(event.key.scancode);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        state.mouse = {event.motion.x, event.motion.y};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        state.mouse = {event.button.x, event.button.y};
        if (const int slot = buttonSlot(event.button.button); slot >= 0) {
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            state.buttonDown[slot] = down;
            (down ? state.buttonPressed : state.buttonReleased)[slot] = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        state.wheel += event.wheel.y;
        break;
    case SDL_EVENT_TEXT_INPUT:
        appendUtf8Codepoints(event.text.text);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        state.down.reset();
        state.buttonDown.fill(false);
        break;
    default:
        break;
    }
}

bool keyPressed(SDL_Scancode key) { return state.pressed.test(key); }

bool keyRepeated(SDL_Scancode key) { return state.repeated.test(key); }

bool keyDown(SDL_Scancode key) { return state.down.test(key); }

bool mousePressed(MouseButton button) { return state.buttonPressed[static_cast<size_t>(button)]; }

bool mouseReleased(MouseButton button) { return state.buttonReleased[static_cast<size_t>(button)]; }

bool mouseDown(MouseButton button) { return state.buttonDown[static_cast<size_t>(button)]; }

Vec2 mousePosition() { return state.mouse; }

float wheelMove() { return state.wheel; }

int nextTypedCharacter() {
    if (state.typed.empty()) {
        return 0;
    }
    const int character = state.typed.front();
    state.typed.pop_front();
    return character;
}

double seconds() { return static_cast<double>(SDL_GetTicksNS()) * 1.0e-9; }

void requestTextInput() { state.textRequested = true; }

bool wantsTextInput() { return state.textRequested; }

}
