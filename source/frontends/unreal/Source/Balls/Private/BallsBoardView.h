#pragma once

#include "BallsCoreIncludes.h"
#include "BallsLayout.h"
#include "BallsRenderer.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

#include <vector>

struct FBallsGraphicsSettings;

namespace BallsBoardView {

inline constexpr float CellImageBelowPixels = 4.0f;

}

class ACameraActor;
class AActor;
class ADirectionalLight;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UWorld;

class FBallsBoardView {
public:
    explicit FBallsBoardView(UWorld* World);
    ~FBallsBoardView();

    void Update(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize, float DpiScale);

    void SubmitBalls(Simulation& Sim, const FBallsGameplayLayout& Layout, float DpiScale);

    void SubmitBalls(const BallSnapshot& Snapshot, float BallRadius, const FBallsGameplayLayout& Layout, float DpiScale);
    static float VisibilitySlotCells(float BallRadius, const FBallsGameplayLayout& Layout, float DpiScale);
    void Hide();

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

    void UpdateStaticScene(const BrickBoard& Board);

    void ShowCellImage(bool bShow);
    void SubmitCellImage(const BrickBoard& Board);
    void FrameCamera(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize);

    UWorld* World = nullptr;
    TStrongObjectPtr<ACameraActor> Camera;
    TStrongObjectPtr<ADirectionalLight> KeyLight;
    TStrongObjectPtr<AActor> BoardActor;

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

    uint64 SentCellSerial = 0;
    std::vector<uint64_t> SentTileRevisions;
};
