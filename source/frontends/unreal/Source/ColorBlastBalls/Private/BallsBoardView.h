#pragma once

#include "BallsCoreIncludes.h"
#include "BallsLayout.h"
#include "BallsRenderer.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

enum class EBallsGraphics;

class ACameraActor;
class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UWorld;

// The 3D side of a game: camera, lights, backdrop and instanced bricks in the world, plus the GPU ball pass.
// One board cell is BallsBoardView::CellWorldSize units; board column grows along +X, board row along -Z,
// and the brick fronts sit on the Y = 0 plane facing the camera at +Y.
class FBallsBoardView {
public:
    explicit FBallsBoardView(UWorld* World);
    ~FBallsBoardView();

    // Frames the camera so the board lands on Layout.Board inside a ViewportSize (both in Slate units).
    void Update(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize, float DpiScale);
    // A session on the game thread: sizes the backend's visibility grid and draws straight from it.
    void SubmitBalls(Simulation& Sim, const FBallsGameplayLayout& Layout, float DpiScale);
    // Balls already copied out, by the physics thread into a SessionView.
    void SubmitBalls(const BallSnapshot& Snapshot, float BallRadius, const FBallsGameplayLayout& Layout, float DpiScale);
    static float VisibilitySlotCells(float BallRadius, const FBallsGameplayLayout& Layout, float DpiScale);
    void Hide();
    // Camera half of the graphics level: bloom and vignette strength, a fixed exposure below High, and how bright
    // the balls are drawn (1.0 once bloom is off, so the palette reads exactly).
    void ApplyGraphics(EBallsGraphics Quality);

private:
    void SpawnScene();
    void RebuildBricks(const BrickBoard& Board);
    // Backdrop, outside field and walls depend only on the board size, not on its bricks.
    void UpdateStaticScene(const BrickBoard& Board);
    void FrameCamera(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize);

    UWorld* World = nullptr;
    TStrongObjectPtr<ACameraActor> Camera;
    TStrongObjectPtr<AActor> BoardActor;
    // Backdrop is exactly the play field; Outside fills the rest of the screen and the four walls (left, right,
    // top, bottom) mark where balls bounce.
    TStrongObjectPtr<UStaticMeshComponent> Backdrop;
    TStrongObjectPtr<UStaticMeshComponent> Outside;
    TArray<TStrongObjectPtr<UStaticMeshComponent>> Walls;
    TArray<TStrongObjectPtr<UInstancedStaticMeshComponent>> BrickMeshes;
    TArray<TStrongObjectPtr<AActor>> SceneActors;
    FBallsRenderer BallRenderer;
    float BallBrightness = 2.2f;
    uint64 BuiltRevision = ~0ull;
    int BuiltColumns = -1;
    int BuiltRows = -1;
    double LastBrickRebuild = 0.0;
    bool bBricksAnimating = false;
    bool bVisible = false;
};
