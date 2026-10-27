#include "screens/LevelSelectScreen.h"

#include "app/App.h"
#include "app/Paths.h"
#include "level/LevelImporter.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <algorithm>
#include <format>

namespace {

constexpr float listWidth = 560.0f;
constexpr float rowHeight = 58.0f;
constexpr float rowGap = 12.0f;
constexpr float editButtonWidth = 110.0f;
constexpr float scrollSpeed = 40.0f;
constexpr float footerButtonWidth = 200.0f;
constexpr float footerButtonHeight = 56.0f;
constexpr float footerGap = 14.0f;
constexpr float folderTextSize = 16.0f;
constexpr float folderLineHeight = 30.0f;
constexpr float importAreaHeight = folderLineHeight + footerButtonHeight + footerGap;
constexpr float highlightSeconds = 2.0f;
constexpr auto refreshInterval = std::chrono::milliseconds(500);

std::filesystem::path absoluteFolder(const std::filesystem::path& folder) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::weakly_canonical(folder, error);
    return error ? std::filesystem::absolute(folder) : absolute;
}

// Long install paths are cut from the left: the end of the path is the part that tells levels folders apart.
std::string fitFolderText(const std::string& text, float maximumWidth) {
    if (ui::measureText(text, folderTextSize).x <= maximumWidth) {
        return text;
    }
    for (size_t cut = 1; cut < text.size(); ++cut) {
        std::string shortened = "..." + text.substr(cut);
        if (ui::measureText(shortened, folderTextSize).x <= maximumWidth) {
            return shortened;
        }
    }
    return text;
}

}

LevelSelectScreen::LevelSelectScreen(App& app, GameMode mode)
    : app(app), mode(mode), levels(findLevels(levelDirectory(mode))), folder(absoluteFolder(levelDirectory(mode))) {}

void LevelSelectScreen::refreshLevels() {
    levels = findLevels(folder);
    nextRefresh = std::chrono::steady_clock::now() + refreshInterval;
}

void LevelSelectScreen::scrollTo(const std::string& levelName) {
    const auto found = std::ranges::find(levels, levelName, &LevelFiles::name);
    if (found == levels.end()) {
        return;
    }
    const float rowTop = static_cast<float>(found - levels.begin()) * (rowHeight + rowGap);
    const float visibleHeight = static_cast<float>(gfx::screenHeight()) - 240.0f - importAreaHeight;
    if (rowTop < scroll || rowTop + rowHeight > scroll + visibleHeight) {
        scroll = std::max(0.0f, rowTop - visibleHeight * 0.5f);
    }
}

bool LevelSelectScreen::importLevels(std::span<const std::filesystem::path> paths) {
    const LevelImportReport report = ::importLevels(paths, folder, mode);
    refreshLevels();
    if (const auto imported = report.levelToHighlight()) {
        highlightedLevel = *imported;
        highlightSecondsLeft = highlightSeconds;
        scrollTo(*imported);
    }
    app.showToast(report.summary(), report.anyFailed());
    return true;
}

std::string LevelSelectScreen::stateJson() const {
    std::string names;
    for (const LevelFiles& level : levels) {
        names += std::format("{}\"{}\"", names.empty() ? "" : ",", jsonEscape(level.name));
    }
    return std::format("{{\"mode\":\"{}\",\"folder\":\"{}\",\"levels\":[{}],\"highlighted\":\"{}\"}}",
                       mode == GameMode::Normal ? "normal" : "performance", jsonEscape(utf8Of(folder)), names,
                       highlightSecondsLeft > 0.0f ? jsonEscape(highlightedLevel) : std::string{});
}

void LevelSelectScreen::drawImportControls(float centerX, float top) {
    const float screenWidth = static_cast<float>(gfx::screenWidth());
    const std::string folderText = fitFolderText("Levels folder: " + utf8Of(folder), screenWidth - 40.0f);
    ui::drawText(folderText, {centerX, top + 6.0f}, folderTextSize, palette::textMuted, ui::Align::Center);
    const float buttonsTop = top + folderLineHeight;
    const Rect importRect{centerX - listWidth * 0.5f, buttonsTop, footerButtonWidth, footerButtonHeight};
    if (ui::button(importRect, "Import")) {
        app.openLevelImportDialog(folder);
    }
    const Rect openRect{centerX + listWidth * 0.5f - footerButtonWidth, buttonsTop, footerButtonWidth,
                        footerButtonHeight};
    if (ui::button(openRect, "Open Folder")) {
        app.openFolder(folder);
    }
}

void LevelSelectScreen::update(float deltaTime) {
    highlightSecondsLeft = std::max(0.0f, highlightSecondsLeft - deltaTime);
    if (std::chrono::steady_clock::now() >= nextRefresh) {
        refreshLevels();
    }
    scroll = std::max(0.0f, scroll - input::wheelMove() * scrollSpeed);
    if (input::keyPressed(SDL_SCANCODE_ESCAPE)) {
        app.showMainMenu();
    }
}

void LevelSelectScreen::draw() {
    const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
    const char* title = mode == GameMode::Normal ? "Normal Mode" : "Performance Mode";
    ui::drawText(title, {centerX, 40.0f}, 52.0f, palette::accent, ui::Align::Center);

    const float top = 130.0f;
    const float bottom = static_cast<float>(gfx::screenHeight()) - 110.0f - importAreaHeight;
    gfx::beginClip({0.0f, top, static_cast<float>(gfx::screenWidth()), bottom - top});
    for (size_t i = 0; i < levels.size(); ++i) {
        const float y = top + static_cast<float>(i) * (rowHeight + rowGap) - scroll;
        const Rect playRect{centerX - listWidth * 0.5f, y, listWidth - editButtonWidth - rowGap, rowHeight};
        const Rect editRect{playRect.x + playRect.width + rowGap, y, editButtonWidth, rowHeight};
        const bool visible = y + rowHeight > top && y < bottom;
        const bool highlighted = highlightSecondsLeft > 0.0f && levels[i].name == highlightedLevel;
        if (ui::button(playRect, levels[i].name, visible, highlighted)) {
            app.startGame(mode, levels[i]);
        }
        if (ui::button(editRect, "Edit", visible)) {
            app.openEditor(mode, levels[i]);
        }
    }
    gfx::endClip();
    if (levels.empty()) {
        ui::drawText("No levels found", {centerX, top + 40.0f}, 28.0f, palette::textMuted, ui::Align::Center);
    }
    drawImportControls(centerX, bottom + 8.0f);
    const float footerTop = bottom + importAreaHeight + 24.0f;
    const Rect backRect{centerX - listWidth * 0.5f, footerTop, 200.0f, 56.0f};
    if (ui::button(backRect, "Back")) {
        app.showMainMenu();
    }
    const Rect newRect{centerX + listWidth * 0.5f - 200.0f, footerTop, 200.0f, 56.0f};
    if (ui::button(newRect, "New Level")) {
        app.openEditor(mode, std::nullopt);
    }
}
