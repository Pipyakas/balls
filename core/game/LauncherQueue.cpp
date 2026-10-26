#include "game/LauncherQueue.h"

#include <algorithm>

namespace {

constexpr float riseSpeedRowsPerSecond = 8.0f;

}

void LauncherQueue::load(const LauncherLayout& layout) {
    columns.assign(static_cast<size_t>(layout.columns), {});
    visibleRowCount = layout.visibleRows;
    rotationColumn = 0;
    for (int row = 0; row < layout.rowCount(); ++row) {
        for (int column = 0; column < layout.columns; ++column) {
            if (const auto& slot = layout.slot(column, row)) {
                columns[column].push_back({*slot, 0.0f});
            }
        }
    }
}

void LauncherQueue::animate(float deltaTime) {
    for (auto& column : columns) {
        for (QueuedLauncher& queued : column) {
            queued.riseOffset = std::max(0.0f, queued.riseOffset - riseSpeedRowsPerSecond * deltaTime);
        }
    }
}

std::optional<Launcher> LauncherQueue::take(int column, int row) {
    if (column < 0 || column >= columnCount() || row < 0 || row >= visibleRowCount) {
        return std::nullopt;
    }
    auto& queue = columns[column];
    if (row >= static_cast<int>(queue.size())) {
        return std::nullopt;
    }
    const Launcher taken = queue[row].launcher;
    queue.erase(queue.begin() + row);
    for (size_t behind = static_cast<size_t>(row); behind < queue.size(); ++behind) {
        queue[behind].riseOffset += 1.0f;
    }
    return taken;
}

std::optional<Launcher> LauncherQueue::takeNextInRotation() {
    for (int attempt = 0; attempt < columnCount(); ++attempt) {
        const int column = (rotationColumn + attempt) % columnCount();
        if (!columns[column].empty()) {
            rotationColumn = (column + 1) % columnCount();
            return take(column, 0);
        }
    }
    return std::nullopt;
}

const QueuedLauncher* LauncherQueue::visible(int column, int row) const {
    if (column < 0 || column >= columnCount() || row < 0 || row >= visibleRowCount) {
        return nullptr;
    }
    const auto& queue = columns[column];
    return row < static_cast<int>(queue.size()) ? &queue[row] : nullptr;
}

bool LauncherQueue::isEmpty() const {
    return std::ranges::all_of(columns, [](const auto& column) { return column.empty(); });
}

int LauncherQueue::launcherCount() const {
    int total = 0;
    for (const auto& column : columns) {
        total += static_cast<int>(column.size());
    }
    return total;
}

ColorTally LauncherQueue::remainingBalls() const {
    ColorTally tally{};
    for (const auto& column : columns) {
        for (const QueuedLauncher& queued : column) {
            tally[toIndex(queued.launcher.color)] += queued.launcher.ballCount;
        }
    }
    return tally;
}
