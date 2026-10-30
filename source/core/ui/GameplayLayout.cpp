#include "ui/GameplayLayout.h"

#include <algorithm>

namespace {

constexpr float outerMargin = 16.0f;
constexpr float gadgetGap = 8.0f;
constexpr float boardPadding = 10.0f;
constexpr float landscapeGadgetWidth = 310.0f;
constexpr float landscapeHeaderHeight = 56.0f;
constexpr float widestLandscapeAspect = 16.0f / 9.0f;
constexpr float portraitTopFraction = 0.046f;
constexpr float portraitTopMinimum = 72.0f;
constexpr float portraitTopMaximum = 150.0f;
constexpr float portraitHeaderFraction = 0.55f;
constexpr float minimumBoardExtent = 240.0f;
constexpr float compactLauncherFraction = 0.19f;
constexpr float compactLauncherMinimum = 120.0f;
constexpr float compactLauncherMaximum = 240.0f;
constexpr float compactRowExtra = 48.0f;
constexpr float launcherGapShare = 0.18f;
constexpr float bottomFraction = 0.38f;
constexpr float bottomMinimum = 200.0f;
constexpr float bottomMaximum = 620.0f;
// Cells may be smaller than a pixel: a board with more cells than the window has pixels (2048 columns in a
// 1920-wide window) otherwise grew past the frame and pushed the gadgets off it. This only keeps the size positive.
constexpr float smallestCellSize = 1e-3f;

float panelHeight(float frameHeight, int launcherRows, float frameWidth) {
    const float controlsHeight = std::clamp(frameHeight * bottomFraction, bottomMinimum, bottomMaximum);
    if (launcherRows <= 0) {
        return controlsHeight;
    }
    const float launcher =
        std::clamp(frameWidth * compactLauncherFraction, compactLauncherMinimum, compactLauncherMaximum);
    const float rows = static_cast<float>(launcherRows);
    const float gridHeight = rows * launcher + (rows - 1.0f) * launcher * launcherGapShare;
    return std::min(gridHeight, std::max(controlsHeight - compactRowExtra, launcher)) + compactRowExtra;
}

} // namespace

LayoutOrientation resolveOrientation(LayoutOrientation setting, float width, float height) {
    if (setting != LayoutOrientation::Auto) {
        return setting;
    }
    return height > width ? LayoutOrientation::Portrait : LayoutOrientation::Landscape;
}

CoreGameplayLayout computeCoreGameplayLayout(float frameWidth, float frameHeight, int boardColumns,
                                             int boardRows, LayoutOrientation orientation,
                                             int launcherRows) {
    const int columns = std::max(boardColumns, 1);
    const int rows = std::max(boardRows, 1);
    CoreGameplayLayout layout;
    layout.isPortrait =
        resolveOrientation(orientation, frameWidth, frameHeight) == LayoutOrientation::Portrait;
    if (!layout.isPortrait) {
        const float columnHeight = std::max(frameHeight - outerMargin * 2.0f, 1.0f);
        const float boardWidth = frameWidth - outerMargin * 2.0f - landscapeGadgetWidth * 2.0f -
                                 gadgetGap * 2.0f - boardPadding * 2.0f;
        const float boardHeight = frameHeight - outerMargin * 2.0f - boardPadding * 2.0f;
        layout.cellSize = std::max(smallestCellSize, std::min(boardWidth / static_cast<float>(columns),
                                                              boardHeight / static_cast<float>(rows)));
        const float frameBoardWidth = layout.cellSize * static_cast<float>(columns) + boardPadding * 2.0f;
        const float frameBoardHeight = layout.cellSize * static_cast<float>(rows) + boardPadding * 2.0f;
        const float minimumGroupWidth = landscapeGadgetWidth * 2.0f + gadgetGap * 2.0f + frameBoardWidth;
        // The board keeps its size; the room it leaves goes to the gadgets, until the whole group reaches 16:9,
        // so wide windows don't show empty bands beside them and ultrawide ones don't stretch them across.
        const float widestGroup = std::min(frameWidth, frameHeight * widestLandscapeAspect) - outerMargin * 2.0f;
        const float gadgetGrowth = std::max(0.0f, widestGroup - minimumGroupWidth) * 0.5f;
        const float gadgetWidth = landscapeGadgetWidth + gadgetGrowth;
        const float groupWidth = minimumGroupWidth + gadgetGrowth * 2.0f;
        const float groupX = (frameWidth - groupWidth) * 0.5f;
        layout.leftGadget = {groupX, outerMargin, gadgetWidth, columnHeight};
        layout.header = {groupX, outerMargin, gadgetWidth, landscapeHeaderHeight};
        layout.interaction = {groupX, outerMargin + landscapeHeaderHeight + gadgetGap, gadgetWidth,
                              columnHeight - landscapeHeaderHeight - gadgetGap};
        layout.main = {groupX + gadgetWidth + gadgetGap,
                       outerMargin + (columnHeight - frameBoardHeight) * 0.5f, frameBoardWidth,
                       frameBoardHeight};
        layout.rightGadget = {layout.main.x + frameBoardWidth + gadgetGap, outerMargin, gadgetWidth,
                              columnHeight};
        layout.debug = layout.rightGadget;
    } else {
        // The top gadget is a fraction of the frame (4.6% of the 1560-unit portrait design), so the debug
        // strip keeps room for its label and value lines on a tall phone instead of clipping them.
        const float topHeight =
            std::clamp(frameHeight * portraitTopFraction, portraitTopMinimum, portraitTopMaximum);
        const float headerHeight = topHeight * portraitHeaderFraction;
        float bottomHeight = panelHeight(frameHeight, launcherRows, frameWidth);
        const float boardSpace = frameHeight - outerMargin * 2.0f - topHeight - bottomHeight -
                                 gadgetGap * 2.0f - boardPadding * 2.0f;
        if (boardSpace < minimumBoardExtent) {
            bottomHeight = std::max(0.0f, bottomHeight - (minimumBoardExtent - boardSpace));
        }
        const float boardWidth = frameWidth - outerMargin * 2.0f - boardPadding * 2.0f;
        const float boardHeight = frameHeight - outerMargin * 2.0f - topHeight - bottomHeight -
                                  gadgetGap * 2.0f - boardPadding * 2.0f;
        layout.cellSize = std::max(smallestCellSize, std::min(boardWidth / static_cast<float>(columns),
                                                              boardHeight / static_cast<float>(rows)));
        const float frameBoardWidth = layout.cellSize * static_cast<float>(columns) + boardPadding * 2.0f;
        const float frameBoardHeight = layout.cellSize * static_cast<float>(rows) + boardPadding * 2.0f;
        const float groupHeight = topHeight + gadgetGap + frameBoardHeight + gadgetGap + bottomHeight;
        const float groupY = (frameHeight - groupHeight) * 0.5f;
        const float gadgetWidth = frameWidth - outerMargin * 2.0f;
        layout.topGadget = {outerMargin, groupY, gadgetWidth, topHeight};
        layout.header = {outerMargin, groupY, gadgetWidth, headerHeight};
        layout.debug = {outerMargin, groupY + headerHeight, gadgetWidth, topHeight - headerHeight};
        layout.main = {outerMargin + (gadgetWidth - frameBoardWidth) * 0.5f,
                       groupY + topHeight + gadgetGap, frameBoardWidth, frameBoardHeight};
        layout.bottomGadget = {outerMargin, layout.main.y + frameBoardHeight + gadgetGap, gadgetWidth,
                               bottomHeight};
        layout.interaction = layout.bottomGadget;
    }
    layout.boardOrigin = {layout.main.x + boardPadding, layout.main.y + boardPadding};
    layout.boardRect = {layout.boardOrigin.x, layout.boardOrigin.y,
                        layout.cellSize * static_cast<float>(columns),
                        layout.cellSize * static_cast<float>(rows)};
    return layout;
}
