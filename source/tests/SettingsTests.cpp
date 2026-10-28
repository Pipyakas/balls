#include "TestFramework.h"

#include "game/Settings.h"

TEST_CASE("settings default to the game rules and automatic layout") {
    const Settings settings;
    EXPECT(!settings.ballCollisions);
    EXPECT(settings.layout == LayoutOrientation::Auto);
    EXPECT(!settings.vsync);
    EXPECT(settings.defaultBackend.empty());
    EXPECT(settings.renderer == "webgpu");
    EXPECT(settings.graphicsApi == "auto");
}

TEST_CASE("settings survive a serialize and parse round trip") {
    const Settings original{
        .ballCollisions = true,
        .layout = LayoutOrientation::Portrait,
        .vsync = true,
        .defaultBackend = "AVX2@cpu+CUDA@gpu0",
        .renderer = "webgpu",
        .graphicsApi = "vulkan",
        .advancedSettings = true,
    };
    EXPECT(parseSettings(serializeSettings(original)) == original);
}

TEST_CASE("vsync parses on and off while bad values keep the default") {
    EXPECT(parseSettings("vsync=on\n").vsync);
    EXPECT(!parseSettings("vsync=off\n").vsync);
    EXPECT(!parseSettings("vsync=maybe\n").vsync);
}

TEST_CASE("settings ignore unknown keys and keep defaults for bad values") {
    const Settings parsed = parseSettings("futureOption=7\nballCollisions=maybe\nlayout = landscape\r\nrenderer=WebGPU\r\napi=D3D12\n");
    EXPECT(!parsed.ballCollisions);
    EXPECT(parsed.layout == LayoutOrientation::Landscape);
    EXPECT(parsed.renderer == "webgpu");
    EXPECT(parsed.graphicsApi == "d3d12");
}
