#pragma once

#include "sim/BrickBoard.h"

#include <array>
#include <memory>

// The boards the UI draws from, kept level with the live board by replaying its change log instead of copying it
// every frame. Two are enough: the UI holds the latest one (and maybe one it is still drawing) while the other is
// brought up to date.
class BoardSnapshots {
public:
    std::shared_ptr<const BrickBoard> publish(const BrickBoard& live, const std::shared_ptr<const BrickBoard>& previous);

private:
    std::array<std::shared_ptr<BrickBoard>, 2> boards;
};
