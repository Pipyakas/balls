#pragma once

#include "common/BallCount.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <map>
#include <optional>

class MachineProfile {
public:
    void load();
    void rememberBallLimit(BackendKind backend, BallCount limit);
    std::optional<BallCount> ballLimit(BackendKind backend) const;

private:
    std::filesystem::path filePath() const;
    void save() const;

    std::map<BackendKind, BallCount> ballLimits;
};
