#include "ui/GameplayLayout.h"

#include <algorithm>

namespace {

constexpr float outerMargin = 16.0f;
constexpr float gadgetGap = 8.0f;
constexpr float boardPadding = 10.0f;
constexpr float landscapeLeftWidth = 420.0f;
constexpr float landscapeRightWidth = 200.0f;
constexpr float landscapeHeaderHeight = 56.0f;
constexpr float widestLandscapeAspect = 16.0f / 9.0f;
constexpr float portraitTopHeight = 72.0f;
constexpr float portraitHeaderHeight = 44.0f;
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
        const float minimumGroupWidth =
            landscapeLeftWidth + gadgetGap + frameBoardWidth + gadgetGap + landscapeRightWidth;
        // The board keeps its size; the room it leaves goes to the gadgets, until the whole group reaches 16:9,
        // so wide windows don't show empty bands beside them and ultrawide ones don't stretch them across.
        const float widestGroup = std::min(frameWidth, frameHeight * widestLandscapeAspect) - outerMargin * 2.0f;
        const float gadgetGrowth = std::max(0.0f, widestGroup - minimumGroupWidth) * 0.5f;
        const float leftWidth = landscapeLeftWidth + gadgetGrowth;
        const float rightWidth = landscapeRightWidth + gadgetGrowth;
        const float groupWidth = minimumGroupWidth + gadgetGrowth * 2.0f;
        const float groupX = (frameWidth - groupWidth) * 0.5f;
        layout.leftGadget = {groupX, outerMargin, leftWidth, columnHeight};
        layout.header = {groupX, outerMargin, leftWidth, landscapeHeaderHeight};
        layout.interaction = {groupX, outerMargin + landscapeHeaderHeight + gadgetGap, leftWidth,
                              columnHeight - landscapeHeaderHeight - gadgetGap};
        layout.main = {groupX + leftWidth + gadgetGap,
                       outerMargin + (columnHeight - frameBoardHeight) * 0.5f, frameBoardWidth,
                       frameBoardHeight};
        layout.rightGadget = {layout.main.x + frameBoardWidth + gadgetGap, outerMargin, rightWidth,
                              columnHeight};
        layout.debug = layout.rightGadget;
    } else {
        float bottomHeight = panelHeight(frameHeight, compactControls, frameWidth);
        const float boardSpace = frameHeight - outerMargin * 2.0f - portraitTopHeight - bottomHeight -
                                 gadgetGap * 2.0f - boardPadding * 2.0f;
        if (boardSpace < minimumBoardExtent) {
            bottomHeight = std::max(0.0f, bottomHeight - (minimumBoardExtent - boardSpace));
        }
        const float boardWidth = frameWidth - outerMargin * 2.0f - boardPadding * 2.0f;
        const float boardHeight = frameHeight - outerMargin * 2.0f - portraitTopHeight - bottomHeight -
                                  gadgetGap * 2.0f - boardPadding * 2.0f;
        layout.cellSize = std::max(
            1.0f, std::min(boardWidth / static_cast<float>(columns), boardHeight / static_cast<float>(rows)));
        const float frameBoardWidth = layout.cellSize * static_cast<float>(columns) + boardPadding * 2.0f;
        const float frameBoardHeight = layout.cellSize * static_cast<float>(rows) + boardPadding * 2.0f;
        const float groupHeight = portraitTopHeight + gadgetGap + frameBoardHeight + gadgetGap + bottomHeight;
        const float groupY = (frameHeight - groupHeight) * 0.5f;
        const float gadgetWidth = frameWidth - outerMargin * 2.0f;
        layout.topGadget = {outerMargin, groupY, gadgetWidth, portraitTopHeight};
        layout.header = {outerMargin, groupY, gadgetWidth, portraitHeaderHeight};
        layout.debug = {outerMargin, groupY + portraitHeaderHeight, gadgetWidth,
                        portraitTopHeight - portraitHeaderHeight};
        layout.main = {outerMargin + (gadgetWidth - frameBoardWidth) * 0.5f,
                       groupY + portraitTopHeight + gadgetGap, frameBoardWidth, frameBoardHeight};
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
