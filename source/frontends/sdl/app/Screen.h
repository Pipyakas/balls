#pragma once

#include "control/ControlApi.h"

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

    virtual ControlGame* controlGame() { return nullptr; }

    virtual bool importLevels(std::span<const std::filesystem::path>) { return false; }

    virtual float toastTop() const { return defaultToastTop; }

protected:
    static constexpr float defaultToastTop = 24.0f;
};
