#pragma once

#include "app/Screen.h"
#include "editor/LevelEditor.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include "app/Input.h"
#include "ui/Painter.h"

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
        Rect tools;
        Rect properties;
        Rect brickArea;
        Rect launcherArea;
    };

    struct GridCell {
        int column = -1;
        int row = -1;
        bool isValid() const { return column >= 0; }
    };

    Regions computeRegions() const;
    Rect brickGridRect(Rect area) const;
    Rect launcherGridRect(Rect area) const;
    float brickCellSize(Rect area) const;
    float launcherCellSize(Rect area) const;
    GridCell brickCellAt(Rect area, Vec2 point) const;
    GridCell launcherCellAt(Rect area, Vec2 point) const;

    void handleShortcuts();
    void handleBrickInput(Rect area);
    void handleLauncherInput(Rect area);
    void applyBrickTool(GridCell cell, bool erasing);
    void selectColor(BallColor color);
    void changeSelectedHits(int hits);
    bool save();
    void showMessage(std::string text, bool isError);

    void drawToolPanel(Rect area);
    void drawPropertyPanel(Rect area);
    void drawBrickGrid(Rect area);
    void drawLauncherGrid(Rect area);
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
