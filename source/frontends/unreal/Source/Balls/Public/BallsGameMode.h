#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "BallsGameMode.generated.h"

class FBallsApp;

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
