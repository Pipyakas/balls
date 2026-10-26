#pragma once

#include "app/Screen.h"
#include "editor/LevelEditor.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include <raylib.h>

#include <optional>
#include <string>

class App;

enum class EditorTool { Paint, Extend, Erase, Pick };

class MapEditorScreen final : public Screen {
public:
    MapEditorScreen(App& app, GameMode mode, std::optional<LevelFiles> files);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "editor"; }

private:
    struct Regions {
        Rectangle tools;
        Rectangle properties;
        Rectangle brickArea;
        Rectangle launcherArea;
    };

    struct GridCell {
        int column = -1;
        int row = -1;
        bool isValid() const { return column >= 0; }
    };

    Regions computeRegions() const;
    Rectangle brickGridRect(Rectangle area) const;
    Rectangle launcherGridRect(Rectangle area) const;
    float brickCellSize(Rectangle area) const;
    float launcherCellSize(Rectangle area) const;
    GridCell brickCellAt(Rectangle area, Vector2 point) const;
    GridCell launcherCellAt(Rectangle area, Vector2 point) const;

    void handleShortcuts();
    void handleBrickInput(Rectangle area);
    void handleLauncherInput(Rectangle area);
    void applyBrickTool(GridCell cell, bool erasing);
    void selectColor(BallColor color);
    void changeSelectedHits(int hits);
    bool save();
    void showMessage(std::string text, bool isError);

    void drawToolPanel(Rectangle area);
    void drawPropertyPanel(Rectangle area);
    void drawBrickGrid(Rectangle area);
    void drawLauncherGrid(Rectangle area);
    void drawMessage();

    App& app;
    GameMode targetMode;
    LevelEditor editor;
    std::string levelName;
    bool nameFocused = false;
    EditorTool tool = EditorTool::Paint;
    BallColor brushColor = BallColor::Red;
    int brushHits = 1;
    int launcherBallCount = 10;
    int selectedBrick = -1;
    GridCell lastPaintedCell;
    bool draggingBricks = false;
    bool strokeActive = false;
    bool confirmLeave = false;
    std::string message;
    float messageSeconds = 0.0f;
    bool messageIsError = false;
};
