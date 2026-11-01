#include "BallsGameMode.h"

#include "BallsApp.h"

ABallsGameMode::ABallsGameMode() {
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    DefaultPawnClass = nullptr;
}

ABallsGameMode::~ABallsGameMode() = default;

void ABallsGameMode::StartPlay() {
    Super::StartPlay();
    App = MakeShared<FBallsApp>(GetWorld());
}

void ABallsGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    App.Reset();
    Super::EndPlay(EndPlayReason);
}

void ABallsGameMode::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (App) {
        App->Tick(DeltaSeconds);
    }
}
