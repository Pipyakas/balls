#include "TestFramework.h"

#include "game/Settings.h"

TEST_CASE("settings default to the game rules and automatic layout") {
    const Settings settings;
    EXPECT(!settings.ballCollisions);
    EXPECT(settings.layout == LayoutOrientation::Auto);
}

TEST_CASE("settings survive a serialize and parse round trip") {
    const Settings original{.ballCollisions = true, .layout = LayoutOrientation::Portrait};
    EXPECT(parseSettings(serializeSettings(original)) == original);
}

TEST_CASE("settings ignore unknown keys and keep defaults for bad values") {
    const Settings parsed = parseSettings("futureOption=7\nballCollisions=maybe\nlayout = landscape\r\n");
    EXPECT(!parsed.ballCollisions);
    EXPECT(parsed.layout == LayoutOrientation::Landscape);
}
