#include "screens/LevelSelectScreen.h"

#include "app/App.h"
#include "app/Input.h"
#include "app/Paths.h"
#include "render/Palette.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

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

LevelSelectScreen::LevelSelectScreen(App& app, GameMode mode) : app(app), mode(mode) {}

float LevelSelectScreen::listBottom() const {
    return static_cast<float>(gfx::screenHeight()) - 110.0f - importAreaHeight;
}

float LevelSelectScreen::toastTop() const {
    constexpr float toastHeight = 48.0f;
    return listBottom() + 8.0f - toastHeight - footerGap;
}

std::string LevelSelectScreen::stateJson() const {
    std::string names;
    for (const FlowLevelRow& row : app.flow().levelRows()) {
        names += std::format("{}\"{}\"", names.empty() ? "" : ",", jsonEscape(row.name));
    }
    std::string highlighted;
    for (const FlowLevelRow& row : app.flow().levelRows()) {
        if (row.highlighted) {
            highlighted = row.name;
            break;
        }
    }
    return std::format("{{\"mode\":\"{}\",\"folder\":\"{}\",\"levels\":[{}],\"highlighted\":\"{}\"}}",
                       app.flow().mode() == GameMode::Normal ? "normal" : "performance",
                       jsonEscape(utf8Of(app.flow().levelFolder())), names,
                       jsonEscape(highlighted));
}

void LevelSelectScreen::drawImportControls(float centerX, float top) {
    const float screenWidth = static_cast<float>(gfx::screenWidth());
    const std::string folderText = fitFolderText(app.flow().footnote(), screenWidth - 40.0f);
    ui::drawText(folderText, {centerX, top + 6.0f}, folderTextSize, palette::textMuted, ui::Align::Center);
    const float buttonsTop = top + folderLineHeight;
    const Rect importRect{centerX - listWidth * 0.5f, buttonsTop, footerButtonWidth, footerButtonHeight};
    if (ui::button(importRect, "Import")) {
        app.flow().activate(FlowAction::Import);
    }
    const Rect openRect{centerX + listWidth * 0.5f - footerButtonWidth, buttonsTop, footerButtonWidth,
                        footerButtonHeight};
    if (ui::button(openRect, "Open Folder")) {
        app.flow().activate(FlowAction::OpenFolder);
    }
}

void LevelSelectScreen::update(float) {
    if (const auto row = app.flow().takeScrollToRow()) {
        const float rowTop = static_cast<float>(*row) * (rowHeight + rowGap);
        const float visibleHeight = static_cast<float>(gfx::screenHeight()) - 240.0f - importAreaHeight;
        if (rowTop < scroll || rowTop + rowHeight > scroll + visibleHeight) {
            scroll = std::max(0.0f, rowTop - visibleHeight * 0.5f);
        }
    }
    scroll = std::max(0.0f, scroll - input::wheelMove() * scrollSpeed);
}

void LevelSelectScreen::draw() {
    const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
    ui::drawText(app.flow().title(), {centerX, 40.0f}, 52.0f, palette::accent, ui::Align::Center);

    const float top = 130.0f;
    const float bottom = listBottom();
    const auto rows = app.flow().levelRows();
    const float listHeight = rows.empty() ? 0.0f : static_cast<float>(rows.size()) * (rowHeight + rowGap) - rowGap;
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, listHeight - (bottom - top)));
    gfx::beginClip({0.0f, top, static_cast<float>(gfx::screenWidth()), bottom - top});
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = top + static_cast<float>(i) * (rowHeight + rowGap) - scroll;
        const Rect playRect{centerX - listWidth * 0.5f, y, listWidth - editButtonWidth - rowGap, rowHeight};
        const Rect editRect{playRect.x + playRect.width + rowGap, y, editButtonWidth, rowHeight};
        const bool visible = y + rowHeight > top && y < bottom;
        if (ui::button(playRect, rows[i].name, visible, rows[i].highlighted)) {
            app.flow().activate(FlowAction::PlayLevel, static_cast<int>(i));
        }
        if (ui::button(editRect, "Edit", visible)) {
            app.flow().activate(FlowAction::EditLevel, static_cast<int>(i));
        }
    }
    ui::scrollHints({centerX - listWidth * 0.5f, top, listWidth, bottom - top}, scroll, listHeight, palette::background);
    gfx::endClip();
    if (rows.empty()) {
        ui::drawText("No levels found", {centerX, top + 40.0f}, 28.0f, palette::textMuted, ui::Align::Center);
    }
    drawImportControls(centerX, bottom + 8.0f);
    const float footerTop = bottom + importAreaHeight + 24.0f;
    const Rect backRect{centerX - listWidth * 0.5f, footerTop, 200.0f, 56.0f};
    if (ui::button(backRect, "Back")) {
        app.flow().activate(FlowAction::Back);
    }
    const Rect newRect{centerX + listWidth * 0.5f - 200.0f, footerTop, 200.0f, 56.0f};
    if (ui::button(newRect, "New Level")) {
        app.flow().activate(FlowAction::NewLevel);
    }
}
