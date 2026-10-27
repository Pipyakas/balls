#include "BallStepVariants.h"

#include "generated/BallStepBlobs.h"

#include <span>
#include <string_view>

namespace {
struct Entry {
    std::string_view name;
    BallStepBlobSet spirv;
    BallStepBlobSet dxil;
};
} // namespace

static const Entry kTable[] = {
#define BALLS_BALLSTEP_PROFILE(name, tpg, bpt, ct, ci, st, si) \
    {#name, \
     {{ballStep_##name##_BallStepSpirv}, {ballStep_##name##_CountCellsSpirv}, {ballStep_##name##_ScanCellsSpirv}, {ballStep_##name##_StepAndSortSpirv}}, \
     {{ballStep_##name##_BallStepDxil}, {ballStep_##name##_CountCellsDxil}, {ballStep_##name##_ScanCellsDxil}, {ballStep_##name##_StepAndSortDxil}}},
#include "gpu/BallStepProfiles.def"
#undef BALLS_BALLSTEP_PROFILE
};

BallStepBlobSet ballStepSpirvSet(std::string_view variant) {
    if (!variant.empty()) {
        for (const Entry& e : kTable) {
            if (e.name == variant) {
                return e.spirv;
            }
        }
    }
    return kTable[0].spirv;
}

BallStepBlobSet ballStepDxilSet(std::string_view variant) {
    if (!variant.empty()) {
        for (const Entry& e : kTable) {
            if (e.name == variant) {
                return e.dxil;
            }
        }
    }
    return kTable[0].dxil;
}
