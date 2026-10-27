#define SDL_MAIN_USE_CALLBACKS
#include "app/App.h"
#include "app/LaunchOptions.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <string>

SDL_AppResult SDL_AppInit(void** appState, int argumentCount, char** arguments) {
    SDL_SetAppMetadata("Balls", "0.1.0", "io.github.pipyakas.colorblastballs");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    auto* app = new App();
    *appState = app;
    std::string error;
    if (!app->initialize(parseLaunchOptions(argumentCount, arguments), error)) {
        SDL_Log("Startup failed: %s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Balls", error.c_str(), nullptr);
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appState, SDL_Event* event) {
    static_cast<App*>(appState)->handleEvent(*event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appState) {
    return static_cast<App*>(appState)->frame() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* appState, SDL_AppResult) { delete static_cast<App*>(appState); }
