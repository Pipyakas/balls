#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "BallsGameMode.generated.h"

class FBallsApp;

// Runs the whole game from code: the level is the engine's empty Entry map and everything else is spawned here.
UCLASS()
class ABallsGameMode : public AGameModeBase {
    GENERATED_BODY()

public:
    ABallsGameMode();
    ~ABallsGameMode() override;

    void StartPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void Tick(float DeltaSeconds) override;

private:
    TSharedPtr<FBallsApp> App;
};
