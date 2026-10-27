#pragma once

#include "level/LevelData.h"

#include <deque>
#include <optional>
#include <vector>

struct QueuedLauncher {
    Launcher launcher;
    float riseOffset = 0.0f;
};

class LauncherQueue {
public:
    void load(const LauncherLayout& layout);
    void animate(float deltaTime);

    std::optional<Launcher> take(int column, int row);
    std::optional<Launcher> takeNextInRotation();

    int columnCount() const { return static_cast<int>(columns.size()); }
    int visibleRows() const { return visibleRowCount; }
    const QueuedLauncher* visible(int column, int row) const;
    bool isEmpty() const;
    int launcherCount() const;
    ColorTally remainingBalls() const;

private:
    std::vector<std::deque<QueuedLauncher>> columns;
    int visibleRowCount = 0;
    int rotationColumn = 0;
};
