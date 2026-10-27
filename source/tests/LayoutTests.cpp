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
