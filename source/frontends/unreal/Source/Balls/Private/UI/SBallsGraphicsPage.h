#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;

// The Unreal port's Graphics page, a popup over any screen (Settings, or a running game's controls) so every change
// shows at once on the live board: the presets on top, then anti-aliasing and resolution, the UE5 features with the
// largest frame costs, and the engine's scalability groups one by one.
class SBallsGraphicsPage final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsGraphicsPage) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
