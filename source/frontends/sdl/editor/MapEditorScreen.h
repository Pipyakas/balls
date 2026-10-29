#pragma once

#include "app/Screen.h"
#include "editor/EditorSession.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <optional>
#include <string>

class App;

class MapEditorScreen final : public Screen {
public:
    MapEditorScreen(App& app, GameMode mode, std::optional<LevelFiles> files);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "editor"; }

private:
    enum class PortraitPanel { Tools, Level };

    struct Regions {
        Rect tools;
        Rect properties;
        Rect brickArea;
        Rect launcherArea;
        Rect panelTabs;
        bool isPortrait = false;
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
    void selectColor(BallColor color);
    void changeSelectedHits(int hits);
    bool save();

    void drawToolPanel(Rect area);
    void drawPropertyPanel(Rect area);
    void drawPanelTabs(Rect area);
    void drawBrickGrid(Rect area);
    void drawLauncherGrid(Rect area);
    void drawMessage();

    App& app;
    EditorSession session;
    bool nameFocused = false;
    bool strokeInProgress = false;
    PortraitPanel portraitPanel = PortraitPanel::Tools;
    float toolsScroll = 0.0f;
    float propertiesScroll = 0.0f;
    float toolsContentHeight = 0.0f;
    float propertiesContentHeight = 0.0f;
};
