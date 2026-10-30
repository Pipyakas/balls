#pragma once

#include "BallsCoreIncludes.h"
#include "BallsLayout.h"
#include "BallsRenderer.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

#include <vector>

struct FBallsGraphicsSettings;

namespace BallsBoardView {

// Below this many pixels per cell the bricks are plain squares in the SDL front end too, and a board of them can hold
// millions: they are drawn from the core's cell image in one pass, and no brick is large enough for a label.
inline constexpr float CellImageBelowPixels = 4.0f;

}

class ACameraActor;
class AActor;
class ADirectionalLight;
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
    // Scene half of the graphics settings: bloom and vignette strength from the post-processing level, how bright
    // the balls are drawn (1.0 once bloom is off, so the palette reads exactly), and the key light's shadows.
    void ApplyGraphics(const FBallsGraphicsSettings& Settings);

private:
    struct FBrickSignature {
        bool bAlive = false;
        float HitFlash = 0.0f;
        float RiseOffset = 0.0f;
        int32 RowSum = 0;

        bool operator==(const FBrickSignature& Other) const {
            return bAlive == Other.bAlive && HitFlash == Other.HitFlash &&
                   RiseOffset == Other.RiseOffset && RowSum == Other.RowSum;
        }
        bool operator!=(const FBrickSignature& Other) const {
            return !(*this == Other);
        }
    };

    struct FCachedBrick {
        int32 Color = 0;
        int32 FirstInstance = 0;
        int32 CellCount = 0;
        FBrickSignature Signature;
    };

    void SpawnScene();
    void RebuildBricks(const BrickBoard& Board);
    void UpdateDirtyBricks(const BrickBoard& Board);
    static FBrickSignature MakeSignature(const Brick& Piece);
    static void BuildBrickTransforms(const BrickBoard& Board, int32 BrickIndex, const Brick& Piece,
                                     TArray<FTransform>& OutTransforms);
    // Backdrop, outside field and walls depend only on the board size, not on its bricks.
    void UpdateStaticScene(const BrickBoard& Board);
    // Boards this fine are drawn from the core's cell image in one pass instead of one instance per brick cell.
    void ShowCellImage(bool bShow);
    void SubmitCellImage(const BrickBoard& Board);
    void FrameCamera(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize);

    UWorld* World = nullptr;
    TStrongObjectPtr<ACameraActor> Camera;
    TStrongObjectPtr<ADirectionalLight> KeyLight;
    TStrongObjectPtr<AActor> BoardActor;
    // Backdrop is exactly the play field; Outside fills the rest of the screen and the four walls (left, right,
    // top, bottom) mark where balls bounce.
    TStrongObjectPtr<UStaticMeshComponent> Backdrop;
    TStrongObjectPtr<UStaticMeshComponent> Outside;
    TArray<TStrongObjectPtr<UStaticMeshComponent>> Walls;
    TArray<TStrongObjectPtr<UInstancedStaticMeshComponent>> BrickMeshes;
    TArray<TStrongObjectPtr<AActor>> SceneActors;
    FBallsRenderer BallRenderer;
    TArray<FCachedBrick> CachedBricks;
    float BallBrightness = 2.2f;
    uint64 BuiltRevision = ~0ull;
    int BuiltColumns = -1;
    int BuiltRows = -1;
    int BuiltBrickCount = -1;
    double LastBrickRebuild = 0.0;
    bool bBricksAnimating = false;
    bool bVisible = false;
    bool bShowingCells = false;
    // Which board the render thread's copy of the cell image holds, and each tile's revision when it was sent.
    uint64 SentCellSerial = 0;
    std::vector<uint64_t> SentTileRevisions;
};
