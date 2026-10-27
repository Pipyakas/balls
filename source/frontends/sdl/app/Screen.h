#pragma once

#include "control/ControlProtocol.h"

#include <filesystem>
#include <optional>
#include <span>
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
    // Level files picked in the import dialog or dropped on the window. Screens that cannot take them return
    // false and the app tells the player where imports work.
    virtual bool importLevels(std::span<const std::filesystem::path>) { return false; }
    // Where the app's toast goes on this screen; the design default is the top of the safe area.
    virtual float toastTop() const { return defaultToastTop; }

protected:
    static constexpr float defaultToastTop = 24.0f;
};
