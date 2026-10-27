#define SDL_MAIN_USE_CALLBACKS
#include "app/App.h"
#include "app/LaunchOptions.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>

#if !defined(__ANDROID__)
static void SDLCALL logOutput(void* /*userdata*/, int /*category*/, SDL_LogPriority /*priority*/, const char* message) {
    std::fprintf(stderr, "%s\n", message);
    std::fflush(stderr);
}
#endif

SDL_AppResult SDL_AppInit(void** appState, int argumentCount, char** arguments) {
#if !defined(__ANDROID__)
    SDL_SetLogOutputFunction(logOutput, nullptr);
#endif
    // The identifier matches the Android applicationId, so a log or a crash report names the same app everywhere.
    SDL_SetAppMetadata("Balls", "0.1.0", "com.the company.colorblastballs.sdl");
    // An empty orientation list leaves the device free to rotate: the manifest asks for landscape, and the
    // portrait layout only appears once the activity itself follows the sensor.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "");
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
