#include "game/BoardSnapshots.h"

#include <algorithm>

std::shared_ptr<const BrickBoard> BoardSnapshots::publish(const BrickBoard& live,
                                                          const std::shared_ptr<const BrickBoard>& previous) {
    const bool previousIsOurs = previous && std::ranges::any_of(boards, [&](const auto& board) { return board == previous; });
    if (previousIsOurs && previous->layoutSerial() == live.layoutSerial() &&
        previous->followedPosition() == live.changeLogEnd()) {
        return previous;
    }
    // Ours is the only reference to a board the UI no longer holds.
    const auto free = std::ranges::find_if(boards, [](const auto& board) { return !board || board.use_count() == 1; });
    if (free == boards.end()) {
        return previous;
    }
    if (!*free) {
        *free = std::make_shared<BrickBoard>();
    }
    (*free)->catchUpWith(live);
    uint64_t oldest = (*free)->followedPosition();
    for (const auto& board : boards) {
        if (board && board->layoutSerial() == live.layoutSerial()) {
            oldest = std::min(oldest, board->followedPosition());
        }
    }
    live.trimChangeLog(oldest);
    return *free;
}
