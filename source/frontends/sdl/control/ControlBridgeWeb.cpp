#include "control/WebControlBridge.h"

#include <SDL3/SDL_log.h>
#include <emscripten/emscripten.h>

#include <string>

namespace {

std::function<std::optional<ControlResponse>(const ControlRequest&)> handler;

}

void setWebControlBridge(std::function<std::optional<ControlResponse>(const ControlRequest&)> newHandler) {
    handler = std::move(newHandler);
    SDL_Log("[control] web bridge registered");

    EM_ASM(window.ballsReady = true);
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* ballsControl(const char* target) {
    static std::string response;
    if (!handler) {
        response = R"({"ok":false,"error":"the game is still loading"})";
        return response.c_str();
    }
    const std::optional<ControlResponse> answer = handler(parseControlTarget(target));
    response = answer ? answer->json : std::string(R"({"ok":false,"error":"unknown route"})");
    return response.c_str();
}
