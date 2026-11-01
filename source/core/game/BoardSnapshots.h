#pragma once

#include "sim/BrickBoard.h"

#include <array>
#include <memory>

class BoardSnapshots {
public:
    std::shared_ptr<const BrickBoard> publish(const BrickBoard& live, const std::shared_ptr<const BrickBoard>& previous);

private:
    std::array<std::shared_ptr<BrickBoard>, 2> boards;
};
