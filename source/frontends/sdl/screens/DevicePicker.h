#pragma once

#include "sim/BackendSelection.h"
#include "ui/Painter.h"

#include <optional>
#include <span>
#include <vector>

struct DevicePickerStyle {
    float rowHeight = 40.0f;
    float gap = 8.0f;
    float labelShare = 0.3f;
    float textSize = 18.0f;
};

float devicePickerHeight(const DevicePickerStyle& style);

std::optional<std::vector<BackendPick>> drawDevicePicker(Rect area, std::span<const BackendPick> current,
                                                         const DevicePickerStyle& style);
