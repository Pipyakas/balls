#include "render/ScreenLayout.h"

#include <algorithm>

namespace {

constexpr float outerMargin = 16.0f;
constexpr float headerHeight = 56.0f;
constexpr float debugPanelWidth = 300.0f;
constexpr float interactionShare = 0.30f;
constexpr float sectionGap = 12.0f;
constexpr float boardPadding = 10.0f;

}

GameplayLayout computeGameplayLayout(Vector2 screenSize, int boardColumns, int boardRows) {
    GameplayLayout layout;
    const float playWidth = screenSize.x - debugPanelWidth - outerMargin * 3.0f;
    layout.header = {outerMargin, outerMargin, playWidth, headerHeight};
    layout.debugSection = {screenSize.x - debugPanelWidth - outerMargin, outerMargin, debugPanelWidth,
                           screenSize.y - outerMargin * 2.0f};

    const float contentTop = layout.header.y + headerHeight + sectionGap;
    const float contentHeight = screenSize.y - contentTop - outerMargin;
    const float interactionHeight = contentHeight * interactionShare;
    const float mainHeight = contentHeight - interactionHeight - sectionGap;
    layout.mainSection = {outerMargin, contentTop, playWidth, mainHeight};
    layout.interactionSection = {outerMargin, contentTop + mainHeight + sectionGap, playWidth, interactionHeight};

    const float availableWidth = layout.mainSection.width - boardPadding * 2.0f;
    const float availableHeight = layout.mainSection.height - boardPadding * 2.0f;
    const float cell = std::max(1.0f, std::min(availableWidth / static_cast<float>(std::max(boardColumns, 1)),
                                               availableHeight / static_cast<float>(std::max(boardRows, 1))));
    const float boardWidth = cell * static_cast<float>(boardColumns);
    const float boardHeight = cell * static_cast<float>(boardRows);
    layout.board.cellPixels = cell;
    layout.board.origin = {layout.mainSection.x + (layout.mainSection.width - boardWidth) * 0.5f,
                           layout.mainSection.y + (layout.mainSection.height - boardHeight) * 0.5f};
    layout.boardRect = {layout.board.origin.x, layout.board.origin.y, boardWidth, boardHeight};
    return layout;
}
