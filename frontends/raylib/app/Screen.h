#pragma once

#include "control/ControlProtocol.h"

#include <optional>
#include <string>
#include <string_view>

class Screen {
public:
    virtual ~Screen() = default;
    virtual void update(float deltaTime) = 0;
    virtual void draw() = 0;
    virtual std::string_view name() const = 0;
    virtual bool drawsFrameTimings() const { return false; }
    virtual std::string stateJson() const { return "{}"; }
    virtual std::optional<ControlResponse> handleControl(const ControlRequest&) { return std::nullopt; }
};
