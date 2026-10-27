#include "TestFramework.h"
#include "ui/GameplayLayout.h"

#include <cmath>

namespace {

constexpr float gadgetGap = 8.0f;
constexpr float epsilon = 0.01f;

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < epsilon;
}

bool insideFrame(LayoutRect rect, float frameWidth, float frameHeight) {
    return rect.x >= -epsilon && rect.y >= -epsilon && rect.x + rect.width <= frameWidth + epsilon &&
           rect.y + rect.height <= frameHeight + epsilon;
}

bool overlaps(LayoutRect first, LayoutRect second) {
    return first.x < second.x + second.width - epsilon && second.x < first.x + first.width - epsilon &&
           first.y < second.y + second.height - epsilon && second.y < first.y + first.height - epsilon;
}

CoreGameplayLayout compute(float width, float height, int columns, int rows, LayoutOrientation orientation,
                           bool compact) {
    return computeCoreGameplayLayout(width, height, columns, rows, orientation, compact);
}

} // namespace

TEST_CASE("auto orientation follows the window shape") {
    EXPECT(resolveOrientation(LayoutOrientation::Auto, 1440.0f, 900.0f) == LayoutOrientation::Landscape);
    EXPECT(resolveOrientation(LayoutOrientation::Auto, 900.0f, 1440.0f) == LayoutOrientation::Portrait);
    EXPECT(resolveOrientation(LayoutOrientation::Auto, 800.0f, 800.0f) == LayoutOrientation::Landscape);
    EXPECT(resolveOrientation(LayoutOrientation::Portrait, 1440.0f, 900.0f) == LayoutOrientation::Portrait);
    EXPECT(resolveOrientation(LayoutOrientation::Landscape, 900.0f, 1440.0f) == LayoutOrientation::Landscape);
}

TEST_CASE("forced portrait is chosen even in a wide window") {
    const CoreGameplayLayout layout = compute(1440.0f, 900.0f, 10, 20, LayoutOrientation::Portrait, false);
    EXPECT(layout.isPortrait);
}

TEST_CASE("forced landscape is chosen even in a tall window") {
    const CoreGameplayLayout layout = compute(900.0f, 1440.0f, 10, 20, LayoutOrientation::Landscape, false);
    EXPECT(!layout.isPortrait);
}

TEST_CASE("landscape sections stay inside the frame without overlap") {
    const CoreGameplayLayout layout = compute(1440.0f, 900.0f, 10, 20, LayoutOrientation::Landscape, false);
    EXPECT(!layout.isPortrait);
    EXPECT(insideFrame(layout.leftGadget, 1440.0f, 900.0f));
    EXPECT(insideFrame(layout.main, 1440.0f, 900.0f));
    EXPECT(insideFrame(layout.rightGadget, 1440.0f, 900.0f));
    EXPECT(!overlaps(layout.leftGadget, layout.main));
    EXPECT(!overlaps(layout.main, layout.rightGadget));
    EXPECT(insideFrame(layout.boardRect, 1440.0f, 900.0f));
    EXPECT(layout.boardRect.x >= layout.main.x);
    EXPECT(layout.boardRect.y >= layout.main.y);
    EXPECT(layout.boardRect.x + layout.boardRect.width <= layout.main.x + layout.main.width + epsilon);
    EXPECT(layout.boardRect.y + layout.boardRect.height <= layout.main.y + layout.main.height + epsilon);
}

TEST_CASE("portrait sections stay inside the frame without overlap") {
    const CoreGameplayLayout layout = compute(480.0f, 800.0f, 10, 20, LayoutOrientation::Auto, true);
    EXPECT(layout.isPortrait);
    EXPECT(insideFrame(layout.topGadget, 480.0f, 800.0f));
    EXPECT(insideFrame(layout.main, 480.0f, 800.0f));
    EXPECT(insideFrame(layout.bottomGadget, 480.0f, 800.0f));
    EXPECT(!overlaps(layout.topGadget, layout.main));
    EXPECT(!overlaps(layout.main, layout.bottomGadget));
    EXPECT(layout.boardRect.x >= layout.main.x);
    EXPECT(layout.boardRect.y >= layout.main.y);
    EXPECT(layout.boardRect.x + layout.boardRect.width <= layout.main.x + layout.main.width + epsilon);
    EXPECT(layout.boardRect.y + layout.boardRect.height <= layout.main.y + layout.main.height + epsilon);
    EXPECT(near(layout.interaction.height, layout.bottomGadget.height));
}

TEST_CASE("gadgets sit exactly one gap from the board frame") {
    constexpr float wideLandscapeWidth = 1440.0f;
    constexpr float wideLandscapeHeight = 900.0f;
    constexpr float tallPortraitWidth = 480.0f;
    constexpr float tallPortraitHeight = 900.0f;
    constexpr float squareSize = 800.0f;
    const CoreGameplayLayout wide =
        compute(wideLandscapeWidth, wideLandscapeHeight, 20, 8, LayoutOrientation::Landscape, false);
    EXPECT(near(wide.main.x - (wide.leftGadget.x + wide.leftGadget.width), gadgetGap));
    EXPECT(near(wide.rightGadget.x - (wide.main.x + wide.main.width), gadgetGap));
    const CoreGameplayLayout tall =
        compute(tallPortraitWidth, tallPortraitHeight, 8, 20, LayoutOrientation::Portrait, true);
    EXPECT(near(tall.main.y - (tall.topGadget.y + tall.topGadget.height), gadgetGap));
    EXPECT(near(tall.bottomGadget.y - (tall.main.y + tall.main.height), gadgetGap));
    const CoreGameplayLayout squareLandscape =
        compute(squareSize, squareSize, 10, 10, LayoutOrientation::Landscape, false);
    EXPECT(near(squareLandscape.main.x - (squareLandscape.leftGadget.x + squareLandscape.leftGadget.width),
                gadgetGap));
    EXPECT(near(squareLandscape.rightGadget.x - (squareLandscape.main.x + squareLandscape.main.width),
                gadgetGap));
    const CoreGameplayLayout squarePortrait =
        compute(squareSize, squareSize, 10, 10, LayoutOrientation::Portrait, true);
    EXPECT(near(squarePortrait.main.y - (squarePortrait.topGadget.y + squarePortrait.topGadget.height),
                gadgetGap));
    EXPECT(near(squarePortrait.bottomGadget.y - (squarePortrait.main.y + squarePortrait.main.height),
                gadgetGap));
}

TEST_CASE("auto orientation keeps one gap for every board shape") {
    constexpr float wideFrameWidth = 1440.0f;
    constexpr float wideFrameHeight = 900.0f;
    constexpr float tallFrameWidth = 480.0f;
    constexpr float tallFrameHeight = 900.0f;
    constexpr float squareSize = 800.0f;
    const CoreGameplayLayout wideAuto =
        compute(wideFrameWidth, wideFrameHeight, 20, 8, LayoutOrientation::Auto, false);
    EXPECT(wideAuto.isPortrait == false);
    EXPECT(near(wideAuto.main.x - (wideAuto.leftGadget.x + wideAuto.leftGadget.width), gadgetGap));
    EXPECT(near(wideAuto.rightGadget.x - (wideAuto.main.x + wideAuto.main.width), gadgetGap));
    const CoreGameplayLayout tallAuto =
        compute(tallFrameWidth, tallFrameHeight, 8, 20, LayoutOrientation::Auto, true);
    EXPECT(tallAuto.isPortrait == true);
    EXPECT(near(tallAuto.main.y - (tallAuto.topGadget.y + tallAuto.topGadget.height), gadgetGap));
    EXPECT(near(tallAuto.bottomGadget.y - (tallAuto.main.y + tallAuto.main.height), gadgetGap));
    const CoreGameplayLayout squareAuto =
        compute(squareSize, squareSize, 10, 10, LayoutOrientation::Auto, false);
    EXPECT(squareAuto.isPortrait == false);
    EXPECT(near(squareAuto.main.x - (squareAuto.leftGadget.x + squareAuto.leftGadget.width), gadgetGap));
    EXPECT(near(squareAuto.rightGadget.x - (squareAuto.main.x + squareAuto.main.width), gadgetGap));
}

TEST_CASE("forced orientation keeps one gap against the window shape") {
    constexpr float wideFrameWidth = 1440.0f;
    constexpr float wideFrameHeight = 900.0f;
    constexpr float tallFrameWidth = 480.0f;
    constexpr float tallFrameHeight = 900.0f;
    const CoreGameplayLayout forcedPortraitWide =
        compute(wideFrameWidth, wideFrameHeight, 20, 8, LayoutOrientation::Portrait, false);
    EXPECT(forcedPortraitWide.isPortrait == true);
    EXPECT(near(forcedPortraitWide.main.y -
                    (forcedPortraitWide.topGadget.y + forcedPortraitWide.topGadget.height),
                gadgetGap));
    EXPECT(
        near(forcedPortraitWide.bottomGadget.y - (forcedPortraitWide.main.y + forcedPortraitWide.main.height),
             gadgetGap));
    const CoreGameplayLayout forcedLandscapeTall =
        compute(tallFrameWidth, tallFrameHeight, 8, 20, LayoutOrientation::Landscape, false);
    EXPECT(forcedLandscapeTall.isPortrait == false);
    EXPECT(near(forcedLandscapeTall.main.x -
                    (forcedLandscapeTall.leftGadget.x + forcedLandscapeTall.leftGadget.width),
                gadgetGap));
    EXPECT(near(forcedLandscapeTall.rightGadget.x -
                    (forcedLandscapeTall.main.x + forcedLandscapeTall.main.width),
                gadgetGap));
    const CoreGameplayLayout forcedPortraitSquare =
        compute(800.0f, 800.0f, 10, 10, LayoutOrientation::Portrait, false);
    EXPECT(forcedPortraitSquare.isPortrait == true);
    EXPECT(near(forcedPortraitSquare.main.y -
                    (forcedPortraitSquare.topGadget.y + forcedPortraitSquare.topGadget.height),
                gadgetGap));
    EXPECT(near(forcedPortraitSquare.bottomGadget.y -
                    (forcedPortraitSquare.main.y + forcedPortraitSquare.main.height),
                gadgetGap));
}

TEST_CASE("both orientations keep one gap for wide, tall and square boards") {
    constexpr float frameWidth = 1200.0f;
    constexpr float frameHeight = 800.0f;
    const CoreGameplayLayout wideLandscape =
        compute(frameWidth, frameHeight, 20, 8, LayoutOrientation::Landscape, false);
    EXPECT(near(wideLandscape.main.x - (wideLandscape.leftGadget.x + wideLandscape.leftGadget.width),
                gadgetGap));
    EXPECT(near(wideLandscape.rightGadget.x - (wideLandscape.main.x + wideLandscape.main.width), gadgetGap));
    const CoreGameplayLayout widePortrait =
        compute(frameWidth, frameHeight, 20, 8, LayoutOrientation::Portrait, false);
    EXPECT(near(widePortrait.main.y - (widePortrait.topGadget.y + widePortrait.topGadget.height), gadgetGap));
    EXPECT(near(widePortrait.bottomGadget.y - (widePortrait.main.y + widePortrait.main.height), gadgetGap));
    const CoreGameplayLayout tallLandscape =
        compute(frameWidth, frameHeight, 8, 20, LayoutOrientation::Landscape, false);
    EXPECT(near(tallLandscape.main.x - (tallLandscape.leftGadget.x + tallLandscape.leftGadget.width),
                gadgetGap));
    EXPECT(near(tallLandscape.rightGadget.x - (tallLandscape.main.x + tallLandscape.main.width), gadgetGap));
    const CoreGameplayLayout tallPortrait =
        compute(frameWidth, frameHeight, 8, 20, LayoutOrientation::Portrait, false);
    EXPECT(near(tallPortrait.main.y - (tallPortrait.topGadget.y + tallPortrait.topGadget.height), gadgetGap));
    EXPECT(near(tallPortrait.bottomGadget.y - (tallPortrait.main.y + tallPortrait.main.height), gadgetGap));
    const CoreGameplayLayout squareLandscape =
        compute(frameWidth, frameHeight, 10, 10, LayoutOrientation::Landscape, false);
    EXPECT(near(squareLandscape.main.x - (squareLandscape.leftGadget.x + squareLandscape.leftGadget.width),
                gadgetGap));
    EXPECT(near(squareLandscape.rightGadget.x - (squareLandscape.main.x + squareLandscape.main.width),
                gadgetGap));
    const CoreGameplayLayout squarePortrait =
        compute(frameWidth, frameHeight, 10, 10, LayoutOrientation::Portrait, false);
    EXPECT(near(squarePortrait.main.y - (squarePortrait.topGadget.y + squarePortrait.topGadget.height),
                gadgetGap));
    EXPECT(near(squarePortrait.bottomGadget.y - (squarePortrait.main.y + squarePortrait.main.height),
                gadgetGap));
}

TEST_CASE("portrait keeps compact controls to one launcher row") {
    const float frameWidth = 480.0f;
    const CoreGameplayLayout layout = compute(frameWidth, 800.0f, 10, 20, LayoutOrientation::Portrait, true);
    const float launcher = std::clamp(frameWidth * 0.19f, 120.0f, 240.0f);
    EXPECT(near(layout.bottomGadget.height, launcher + 48.0f));
}

TEST_CASE("portrait clamps the bottom panel and protects the board") {
    const CoreGameplayLayout shortFrame = compute(480.0f, 700.0f, 10, 30, LayoutOrientation::Portrait, false);
    EXPECT(shortFrame.bottomGadget.height <= 620.0f + epsilon);
    EXPECT(shortFrame.boardRect.height >= 240.0f - epsilon);
    const CoreGameplayLayout tallFrame = compute(480.0f, 1600.0f, 10, 30, LayoutOrientation::Portrait, false);
    EXPECT(tallFrame.bottomGadget.height <= 620.0f + epsilon);
}

TEST_CASE("landscape gadgets widen to fill the frame up to 16:9") {
    constexpr float outerMargin = 16.0f;
    constexpr float baseLeftWidth = 420.0f;
    constexpr float baseRightWidth = 200.0f;
    const CoreGameplayLayout filled = compute(1440.0f, 900.0f, 7, 14, LayoutOrientation::Landscape, false);
    EXPECT(near(filled.leftGadget.x, outerMargin));
    EXPECT(near(filled.rightGadget.x + filled.rightGadget.width, 1440.0f - outerMargin));
    EXPECT(filled.leftGadget.width > baseLeftWidth);
    EXPECT(near(filled.leftGadget.width - baseLeftWidth, filled.rightGadget.width - baseRightWidth));
    EXPECT(near(filled.header.width, filled.leftGadget.width));
    EXPECT(near(filled.interaction.width, filled.leftGadget.width));
    EXPECT(near(filled.debug.width, filled.rightGadget.width));

    const CoreGameplayLayout ultrawide = compute(3440.0f, 900.0f, 7, 14, LayoutOrientation::Landscape, false);
    const float groupWidth = ultrawide.rightGadget.x + ultrawide.rightGadget.width - ultrawide.leftGadget.x;
    EXPECT(near(groupWidth, 900.0f * 16.0f / 9.0f - outerMargin * 2.0f));
    EXPECT(near(ultrawide.leftGadget.x, (3440.0f - groupWidth) * 0.5f));
    EXPECT(near(ultrawide.cellSize, filled.cellSize));

    const CoreGameplayLayout boardBound = compute(1440.0f, 900.0f, 40, 10, LayoutOrientation::Landscape, false);
    EXPECT(near(boardBound.leftGadget.width, baseLeftWidth));
    EXPECT(near(boardBound.rightGadget.width, baseRightWidth));
}
