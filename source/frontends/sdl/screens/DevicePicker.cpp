#include "screens/DevicePicker.h"

#include "render/Palette.h"
#include "sim/BackendWorker.h"
#include "sim/DeviceInventory.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <string>

namespace {

constexpr std::string_view offLabel = "Off";

}

float devicePickerHeight(const DevicePickerStyle& style) {
    const size_t rows = physicalDevices().size();
    return rows == 0 ? 0.0f : static_cast<float>(rows) * (style.rowHeight + style.gap) - style.gap;
}

std::optional<std::vector<BackendPick>> drawDevicePicker(Rect area, std::span<const BackendPick> current,
                                                         const DevicePickerStyle& style) {
    std::optional<std::vector<BackendPick>> changed;
    float cursor = area.y;
    const float labelWidth = area.width * style.labelShare;
    for (const PhysicalDevice& device : physicalDevices()) {
        const auto picked = std::ranges::find(current, device.id, &BackendPick::device);
        const std::optional<BackendKind> pickedKind =
            picked == current.end() ? std::nullopt : std::optional<BackendKind>(picked->kind);
        ui::drawTextInRect(physicalDeviceLabel(device), {area.x, cursor, labelWidth - style.gap, style.rowHeight},
                           style.textSize, palette::text, ui::Align::Left);
        const std::vector<BackendKind> kinds = pickableKinds(device);
        const Rect buttons{area.x + labelWidth, cursor, area.width - labelWidth, style.rowHeight};
        const int count = static_cast<int>(kinds.size()) + 1;
        const bool lastOn = pickedKind && current.size() == 1;
        if (ui::button(ui::columnSlice(buttons, 0, count, style.gap), offLabel, !lastOn, !pickedKind) && pickedKind &&
            !lastOn) {
            changed = withDevicePick(current, device.id, std::nullopt);
        }
        for (size_t index = 0; index < kinds.size(); ++index) {
            const BackendKind kind = kinds[index];
            const DeviceBackendOption* option = device.option(kind);
            const bool available = option && deviceBackendStatus(kind, option->pluginDevice).available;
            const bool selected = pickedKind == kind;
            if (ui::button(ui::columnSlice(buttons, static_cast<int>(index) + 1, count, style.gap),
                           backendKindName(kind), available, selected) &&
                available && !selected) {
                changed = withDevicePick(current, device.id, kind);
            }
        }
        cursor += style.rowHeight + style.gap;
    }
    return changed;
}
