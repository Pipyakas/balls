#pragma once

#include "app/Screen.h"

class App;

class SettingsScreen final : public Screen {
public:
    explicit SettingsScreen(App& app) : app(app) {}

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "settings"; }

private:
    App& app;
};
