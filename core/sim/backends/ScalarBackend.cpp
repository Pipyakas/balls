#include "sim/backends/ScalarBackend.h"

void ScalarBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    const BoardView board = boardView();
    for (size_t i = 0; i < store.count; ++i) {
        const int brick =
            stepBallSubsteps(store.x[i], store.y[i], store.vx[i], store.vy[i], store.color[i], store.bounces[i], board, params);
        store.packed[i] = packBallForRender(store.x[i], store.y[i], store.color[i], params.packScaleX, params.packScaleY);
        if (brick != noBrickHit) {
            hits.push_back({static_cast<uint32_t>(i), brick});
        }
    }
}
