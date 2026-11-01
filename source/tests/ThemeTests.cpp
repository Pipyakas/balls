#include "TestFramework.h"

#include "ui/Theme.h"

TEST_CASE("a brick is its ball color times the brick shade, keeping alpha") {
    for (int index = 0; index < ballColorCount; ++index) {
        const ThemeColor ball = theme::ballColor(index);
        const ThemeColor brick = theme::brickColor(index);
        EXPECT(brick.r == static_cast<uint8_t>(ball.r * theme::brickShade));
        EXPECT(brick.g == static_cast<uint8_t>(ball.g * theme::brickShade));
        EXPECT(brick.b == static_cast<uint8_t>(ball.b * theme::brickShade));
        EXPECT(brick.a == ball.a);
    }
}

TEST_CASE("shading saturates and clamps out-of-range ball colors") {
    const ThemeColor flashed = theme::shade(theme::ballColor(BallColor::Orange), theme::brickHitFlash);
    EXPECT(flashed.r == 255);
    EXPECT(theme::ballColor(-1).r == theme::ballColor(0).r);
    EXPECT(theme::ballColor(ballColorCount).b == theme::ballColor(ballColorCount - 1).b);
}
