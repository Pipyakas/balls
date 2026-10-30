#pragma once

#include "BallsDrawData.h"
#include "CoreMinimal.h"

class FBallsSceneViewExtension;

// Game-thread handle to the ball pass. Create one per world; it draws until destroyed.
class BALLSRENDERING_API FBallsRenderer {
public:
    FBallsRenderer();
    ~FBallsRenderer();

    void Submit(FBallsDrawData&& Data);
    void Clear();
    void SubmitCells(FBallsCellImageUpdate&& Update, const FBallsCellDrawData& Draw);
    void ClearCells();

private:
    TSharedPtr<FBallsSceneViewExtension, ESPMode::ThreadSafe> Extension;
};
