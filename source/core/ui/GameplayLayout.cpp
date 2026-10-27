#include "ui/GameplayLayout.h"

#include <algorithm>

namespace {

constexpr float outerMargin = 16.0f;
constexpr float gadgetGap = 8.0f;
constexpr float boardPadding = 10.0f;
constexpr float landscapeLeftWidth = 420.0f;
constexpr float landscapeRightWidth = 200.0f;
constexpr float landscapeHeaderHeight = 56.0f;
constexpr float portraitTopHeight = 72.0f;
constexpr float portraitTopFraction = 0.046f;
constexpr float portraitHeaderFraction = 0.55f;
constexpr float minimumBoardExtent = 240.0f;
constexpr float compactLauncherFraction = 0.19f;
constexpr float compactLauncherMinimum = 120.0f;
constexpr float compactLauncherMaximum = 240.0f;
constexpr float compactRowExtra = 48.0f;
constexpr float bottomFraction = 0.3f;
constexpr float bottomMinimum = 200.0f;
constexpr float bottomMaximum = 620.0f;

float panelHeight(float frameHeight, bool compactControls, float frameWidth) {
    if (compactControls) {
        const float launcher =
            std::clamp(frameWidth * compactLauncherFraction, compactLauncherMinimum, compactLauncherMaximum);
        return launcher + compactRowExtra;
    }
    return std::clamp(frameHeight * bottomFraction, bottomMinimum, bottomMaximum);
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
                                             bool compactControls) {
    const int columns = std::max(boardColumns, 1);
    const int rows = std::max(boardRows, 1);
    CoreGameplayLayout layout;
    layout.isPortrait =
        resolveOrientation(orientation, frameWidth, frameHeight) == LayoutOrientation::Portrait;
    if (!layout.isPortrait) {
        const float columnHeight = std::max(frameHeight - outerMargin * 2.0f, 1.0f);
        const float boardWidth = frameWidth - outerMargin * 2.0f - landscapeLeftWidth - landscapeRightWidth -
                                 gadgetGap * 2.0f - boardPadding * 2.0f;
        const float boardHeight = frameHeight - outerMargin * 2.0f - boardPadding * 2.0f;
        layout.cellSize = std::max(
            1.0f, std::min(boardWidth / static_cast<float>(columns), boardHeight / static_cast<float>(rows)));
        const float frameBoardWidth = layout.cellSize * static_cast<float>(columns) + boardPadding * 2.0f;
        const float frameBoardHeight = layout.cellSize * static_cast<float>(rows) + boardPadding * 2.0f;
        const float groupWidth =
            landscapeLeftWidth + gadgetGap + frameBoardWidth + gadgetGap + landscapeRightWidth;
        const float groupX = (frameWidth - groupWidth) * 0.5f;
        layout.leftGadget = {groupX, outerMargin, landscapeLeftWidth, columnHeight};
        layout.header = {groupX, outerMargin, landscapeLeftWidth, landscapeHeaderHeight};
        layout.interaction = {groupX, outerMargin + landscapeHeaderHeight + gadgetGap, landscapeLeftWidth,
                              columnHeight - landscapeHeaderHeight - gadgetGap};
        layout.main = {groupX + landscapeLeftWidth + gadgetGap,
                       outerMargin + (columnHeight - frameBoardHeight) * 0.5f, frameBoardWidth,
                       frameBoardHeight};
        layout.rightGadget = {layout.main.x + frameBoardWidth + gadgetGap, outerMargin, landscapeRightWidth,
                              columnHeight};
        layout.debug = layout.rightGadget;
    } else {
        // The top gadget is a fraction of the frame (4.6% of the 1560-unit design), so the debug
        // strip keeps room for its label and value lines on a tall phone instead of clipping.
        const float topHeight = std::clamp(frameHeight * portraitTopFraction, portraitTopHeight, 150.0f);
        const float headerHeight = topHeight * portraitHeaderFraction;
        float bottomHeight = panelHeight(frameHeight, compactControls, frameWidth);
        const float boardSpace = frameHeight - outerMargin * 2.0f - topHeight - bottomHeight -
                                 gadgetGap * 2.0f - boardPadding * 2.0f;
        if (boardSpace < minimumBoardExtent) {
            bottomHeight = std::max(0.0f, bottomHeight - (minimumBoardExtent - boardSpace));
        }
        const float boardWidth = frameWidth - outerMargin * 2.0f - boardPadding * 2.0f;
        const float boardHeight = frameHeight - outerMargin * 2.0f - topHeight - bottomHeight -
                                  gadgetGap * 2.0f - boardPadding * 2.0f;
        layout.cellSize = std::max(
            1.0f, std::min(boardWidth / static_cast<float>(columns), boardHeight / static_cast<float>(rows)));
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
