#pragma once

#include "control/ControlProtocol.h"

#include <functional>
#include <optional>

#if defined(__EMSCRIPTEN__)
void setWebControlBridge(std::function<std::optional<ControlResponse>(const ControlRequest&)> handler);
#endif
