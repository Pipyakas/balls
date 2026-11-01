#pragma once

#include "render/IRenderer.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>
#include <string_view>

std::unique_ptr<IRenderer> createWebGpuRenderer(SDL_Window* window, std::string_view backend,
                                                std::string_view gpuPreference, std::string& error);
