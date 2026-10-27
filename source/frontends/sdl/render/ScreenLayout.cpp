#include "render/ScreenLayout.h"

namespace {

GameplayLayout convertLayout(const CoreGameplayLayout& core) {
    GameplayLayout layout;
    layout.header = {core.header.x, core.header.y, core.header.width, core.header.height};
    layout.mainSection = {core.main.x, core.main.y, core.main.width, core.main.height};
    layout.interactionSection = {core.interaction.x, core.interaction.y, core.interaction.width,
                                 core.interaction.height};
    layout.debugSection = {core.debug.x, core.debug.y, core.debug.width, core.debug.height};
    layout.leftGadget = {core.leftGadget.x, core.leftGadget.y, core.leftGadget.width, core.leftGadget.height};
    layout.rightGadget = {core.rightGadget.x, core.rightGadget.y, core.rightGadget.width,
                          core.rightGadget.height};
    layout.topGadget = {core.topGadget.x, core.topGadget.y, core.topGadget.width, core.topGadget.height};
    layout.bottomGadget = {core.bottomGadget.x, core.bottomGadget.y, core.bottomGadget.width,
                           core.bottomGadget.height};
    layout.board.origin = {core.boardOrigin.x, core.boardOrigin.y};
    layout.board.cellPixels = core.cellSize;
    layout.boardRect = {core.boardRect.x, core.boardRect.y, core.boardRect.width, core.boardRect.height};
    layout.isPortrait = core.isPortrait;
    return layout;
}

} // namespace

GameplayLayout computeGameplayLayout(Vec2 screenSize, int boardColumns, int boardRows) {
    return computeGameplayLayout(screenSize, boardColumns, boardRows, LayoutOrientation::Landscape, 0);
}

GameplayLayout computeGameplayLayout(Vec2 screenSize, int boardColumns, int boardRows,
                                     LayoutOrientation orientation, int launcherRows) {
    return convertLayout(computeCoreGameplayLayout(screenSize.x, screenSize.y, boardColumns, boardRows,
                                                   orientation, launcherRows));
}
