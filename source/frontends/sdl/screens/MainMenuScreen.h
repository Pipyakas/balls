#pragma once

#include "app/Screen.h"

class App;

class MainMenuScreen final : public Screen {
public:
    explicit MainMenuScreen(App& app) : app(app) {}

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "menu"; }

private:
    App& app;
    float time = 0.0f;
};
