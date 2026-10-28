#pragma once

#include "sim/BackendSelection.h"
#include "ui/Painter.h"

#include <optional>
#include <span>
#include <vector>

// One row per physical device (the CPU, each GPU): the device's name, then "Off" and one button per backend it can
// run, so each device gets at most one backend. Two or more devices on make Hybrid; one runs that backend alone.
struct DevicePickerStyle {
    float rowHeight = 40.0f;
    float gap = 8.0f;
    float labelShare = 0.3f;
    float textSize = 18.0f;
};

float devicePickerHeight(const DevicePickerStyle& style);
// The picks after the player's click this frame, or nullopt. Never returns an empty list: turning the last device
// off is ignored.
std::optional<std::vector<BackendPick>> drawDevicePicker(Rect area, std::span<const BackendPick> current,
                                                         const DevicePickerStyle& style);
