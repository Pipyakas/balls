#include "BallsBoardView.h"

#include "BallsGraphics.h"
#include "BallsPalette.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace BallsBoardView {

constexpr double CellWorldSize = 100.0;

}

namespace {

constexpr double CameraFieldOfView = 30.0;
constexpr double BrickDepthCells = 0.6;
constexpr double BrickFlashPopCells = 0.25;
constexpr double CellInset = 0.06;
constexpr float MinimumBallPixels = 1.2f;
constexpr double BrickAnimationInterval = 1.0 / 30.0;
// A visibility slot this many ball radii wide keeps one drawn ball per slot; matches the SDL frontend.
constexpr float VisibilitySlotPerRadius = 1.6f;
// What the backends write into a visibility-grid slot no ball won (core's emptyRenderSlot).
constexpr uint32 EmptyRenderSlot = 0xFFFFFFFFu;
// The most balls one frame uploads and draws; a grid snapshot is far smaller, this only bounds an ungridded one.
constexpr size_t MaximumDrawnBalls = 4'000'000;
// The manual exposure (EV) that shows the lit bricks as bright as the SDL front end's flat ones.
constexpr float ManualExposureBias = 13.0f;
const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
// The engine cube with Nanite turned on (Content/Balls/NaniteCube.uasset, the project's one asset): with Nanite off
// the renderer draws its fallback mesh, the plain cube, so it serves every setting.
const TCHAR* NaniteCubePath = TEXT("/Game/Balls/NaniteCube.NaniteCube");
const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

using BallsBoardView::CellImageBelowPixels;
using BallsBoardView::CellWorldSize;

UMaterialInstanceDynamic* MakeColoredMaterial(UObject* Outer, FColor Color, float Roughness) {
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Outer);
    if (Material) {
        Material->SetVectorParameterValue(TEXT("Color"), BallsPalette::Linear(Color));
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    }
    static bool bLoggedMaterial = false;
    if (!bLoggedMaterial) {
        UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: material base %s, instance %s"),
               Base ? TEXT("loaded") : TEXT("MISSING"), Material ? TEXT("created") : TEXT("MISSING"));
        bLoggedMaterial = true;
    }
    return Material;
}

FVector BoardToWorld(double Column, double Row, double Depth) {
    return FVector(Column * CellWorldSize, Depth * CellWorldSize, -Row * CellWorldSize);
}

struct FCellEdges {
    bool bLeft;
    bool bRight;
    bool bTop;
    bool bBottom;
};

FCellEdges OpenEdges(const BrickBoard& Board, int32 BrickIndex, const BrickCell& Cell) {
    auto Differs = [&](int32 Column, int32 Row) {
        const bool bInside = Column >= 0 && Column < Board.columns() && Row >= 0 && Row < Board.rows();
        return !bInside || Board.brickAt(Column, Row) != BrickIndex;
    };
    return {Differs(Cell.column - 1, Cell.row), Differs(Cell.column + 1, Cell.row), Differs(Cell.column, Cell.row - 1),
            Differs(Cell.column, Cell.row + 1)};
}

}

FBallsBoardView::FBallsBoardView(UWorld* InWorld) : World(InWorld) { SpawnScene(); }

FBallsBoardView::~FBallsBoardView() {
    BallRenderer.Clear();
    for (TStrongObjectPtr<AActor>& Actor : SceneActors) {
        if (Actor.IsValid() && IsValid(Actor.Get())) {
            Actor->Destroy();
        }
    }
}

void FBallsBoardView::SpawnScene() {
    FActorSpawnParameters Parameters;
    Parameters.ObjectFlags = RF_Transient;

    ACameraActor* CameraActor = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
    UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
    CameraComponent->SetFieldOfView(CameraFieldOfView);
    CameraComponent->SetConstraintAspectRatio(false);
    CameraComponent->PostProcessSettings.bOverride_BloomIntensity = true;
    CameraComponent->PostProcessSettings.BloomIntensity = 0.9f;
    CameraComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
    CameraComponent->PostProcessSettings.VignetteIntensity = 0.25f;
    // A fixed exposure, as the SDL front end's flat colors: an auto exposure follows what is on the board, so the
    // bricks darkened as balls filled it and the balls blew out on an empty one.
    CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
    CameraComponent->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
    CameraComponent->PostProcessSettings.AutoExposureBias = ManualExposureBias;
    CameraComponent->PostProcessBlendWeight = 1.0f;
    Camera.Reset(CameraActor);
    SceneActors.Emplace(CameraActor);

    ADirectionalLight* Key = World->SpawnActor<ADirectionalLight>(
        FVector::ZeroVector, FRotator(-35.0, -60.0, 0.0), Parameters);
    // Runtime-spawned lights default to Stationary, which does not light dynamic meshes without baked data.
    Key->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Key->GetLightComponent()->SetIntensity(1.2f);
    // Shadows follow the graphics settings (ApplyGraphics); the low presets keep the flat 2.5D look without them.
    Key->GetLightComponent()->SetCastShadows(false);
    KeyLight.Reset(Key);
    SceneActors.Emplace(Key);

    // A soft shadowless light from the camera side keeps brick fronts readable without a sky.
    ADirectionalLight* FillLight = World->SpawnActor<ADirectionalLight>(
        FVector::ZeroVector, FRotator(-10.0, -100.0, 0.0), Parameters);
    FillLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    FillLight->GetLightComponent()->SetIntensity(0.45f);
    FillLight->GetLightComponent()->SetCastShadows(false);
    FillLight->GetLightComponent()->SetLightColor(BallsPalette::Linear(BallsPalette::TextMuted));
    SceneActors.Emplace(FillLight);

    AActor* Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
    UStaticMesh* BrickMesh = LoadObject<UStaticMesh>(nullptr, NaniteCubePath, nullptr, LOAD_NoWarn);
    UE_LOG(LogTemp, Display, TEXT("BallsBoardView: cube mesh %s, Nanite cube %s"), Cube ? TEXT("loaded") : TEXT("MISSING"),
           BrickMesh ? TEXT("loaded") : TEXT("missing, bricks use the plain cube"));
    if (!BrickMesh) {
        BrickMesh = Cube;
    }

    UStaticMeshComponent* BackdropComponent = NewObject<UStaticMeshComponent>(Actor, TEXT("Backdrop"));
    BackdropComponent->SetStaticMesh(Cube);
    BackdropComponent->SetMobility(EComponentMobility::Movable);
    BackdropComponent->SetMaterial(0, MakeColoredMaterial(BackdropComponent, BallsPalette::BoardBackdrop, 0.9f));
    BackdropComponent->SetupAttachment(Root);
    BackdropComponent->RegisterComponent();
    Backdrop.Reset(BackdropComponent);

    UStaticMeshComponent* OutsideComponent = NewObject<UStaticMeshComponent>(Actor, TEXT("Outside"));
    OutsideComponent->SetStaticMesh(Cube);
    OutsideComponent->SetMobility(EComponentMobility::Movable);
    OutsideComponent->SetMaterial(0, MakeColoredMaterial(OutsideComponent, BallsPalette::Background, 0.9f));
    OutsideComponent->SetupAttachment(Root);
    OutsideComponent->RegisterComponent();
    Outside.Reset(OutsideComponent);

    for (int32 Index = 0; Index < 4; ++Index) {
        UStaticMeshComponent* Wall = NewObject<UStaticMeshComponent>(Actor, *FString::Printf(TEXT("Wall%d"), Index));
        Wall->SetStaticMesh(Cube);
        Wall->SetMobility(EComponentMobility::Movable);
        Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Wall->SetMaterial(0, MakeColoredMaterial(Wall, BallsPalette::ControlPressed, 0.6f));
        Wall->SetupAttachment(Root);
        Wall->RegisterComponent();
        Walls.Emplace(Wall);
    }

    for (int32 Color = 0; Color < ballColorCount; ++Color) {
        UInstancedStaticMeshComponent* Bricks =
            NewObject<UInstancedStaticMeshComponent>(Actor, *FString::Printf(TEXT("Bricks%d"), Color));
        Bricks->SetStaticMesh(BrickMesh);
        Bricks->SetMaterial(0, MakeColoredMaterial(Bricks, BallsPalette::Brick(ballColorFromIndex(Color)), 0.35f));
        Bricks->SetMobility(EComponentMobility::Movable);
        Bricks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Bricks->SetupAttachment(Root);
        Bricks->RegisterComponent();
        BrickMeshes.Emplace(Bricks);
    }
    BoardActor.Reset(Actor);
    SceneActors.Emplace(Actor);
    Actor->SetActorHiddenInGame(true);

    if (APlayerController* Player = World->GetFirstPlayerController()) {
        Player->SetViewTarget(CameraActor);
        UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: SpawnScene set the camera view target"));
    } else {
        UE_LOG(LogTemp, Warning, TEXT("BallsBoardView: SpawnScene has no player controller"));
    }
}

void FBallsBoardView::ApplyGraphics(const FBallsGraphicsSettings& Settings) {
    const int32 PostLevel = Settings.Groups[static_cast<int32>(EBallsScalabilityGroup::PostProcess)];
    const bool bFullBloom = PostLevel >= 3;
    // With bloom off the balls are drawn at their palette colour instead of above-white, so nothing clips.
    BallBrightness = bFullBloom ? 2.2f : 1.0f;
    if (KeyLight) {
        KeyLight->GetLightComponent()->SetCastShadows(Settings.Shadows != EBallsShadows::Off);
    }
    UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
    if (!CameraComponent) {
        return;
    }
    FPostProcessSettings& Post = CameraComponent->PostProcessSettings;
    Post.bOverride_BloomIntensity = true;
    Post.BloomIntensity = bFullBloom ? 0.9f : PostLevel > 0 ? 0.5f : 0.0f;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = bFullBloom ? 0.25f : PostLevel > 0 ? 0.15f : 0.0f;
}

void FBallsBoardView::Hide() {
    if (bVisible) {
        BoardActor->SetActorHiddenInGame(true);
        BallRenderer.Clear();
        ShowCellImage(false);
        BuiltRevision = ~0ull;
        BuiltColumns = -1;
        BuiltRows = -1;
        BuiltBrickCount = -1;
        CachedBricks.Empty();
        bVisible = false;
    }
}

FBallsBoardView::FBrickSignature FBallsBoardView::MakeSignature(const Brick& Piece) {
    int32 RowSum = 0;
    for (const BrickCell& Cell : Piece.cells) {
        RowSum += Cell.row;
    }
    return {Piece.isAlive(), Piece.hitFlash, Piece.riseOffset, RowSum};
}

void FBallsBoardView::BuildBrickTransforms(const BrickBoard& Board, int32 BrickIndex, const Brick& Piece,
                                           TArray<FTransform>& OutTransforms) {
    OutTransforms.Reset();
    OutTransforms.Reserve(Piece.cells.size());
    if (Piece.isAlive()) {
        const double Front = BrickFlashPopCells * Piece.hitFlash;
        const double Back = -BrickDepthCells;
        for (const BrickCell& Cell : Piece.cells) {
            const FCellEdges Edges = OpenEdges(Board, BrickIndex, Cell);
            const double Left = Cell.column + (Edges.bLeft ? CellInset : 0.0);
            const double Right = Cell.column + 1.0 - (Edges.bRight ? CellInset : 0.0);
            const double Top = Cell.row + Piece.riseOffset + (Edges.bTop ? CellInset : 0.0);
            const double Bottom = Cell.row + 1.0 + Piece.riseOffset - (Edges.bBottom ? CellInset : 0.0);
            const FVector Center = BoardToWorld((Left + Right) * 0.5, (Top + Bottom) * 0.5, (Front + Back) * 0.5);
            OutTransforms.Emplace(FQuat::Identity, Center,
                                  FVector(Right - Left, Front - Back, Bottom - Top));
        }
    } else {
        for (size_t CellIndex = 0; CellIndex < Piece.cells.size(); ++CellIndex) {
            OutTransforms.Emplace(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
        }
    }
}

void FBallsBoardView::Update(const BrickBoard& Board, const FBallsGameplayLayout& Layout, FVector2D ViewportSize,
                             float DpiScale) {
    // StartPlay can run before the local player controller exists, so make sure the board camera becomes the
    // view target once it does.
    if (APlayerController* Player = World->GetFirstPlayerController()) {
        if (Player->GetViewTarget() != Camera.Get()) {
            Player->SetViewTarget(Camera.Get());
            UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: camera set as view target"));
        }
    }
    if (!bVisible) {
        BoardActor->SetActorHiddenInGame(false);
        bVisible = true;
    }

    const double Now = FPlatformTime::Seconds();
    const bool bCellImage = Layout.CellSize * DpiScale < CellImageBelowPixels;
    ShowCellImage(bCellImage);
    if (bCellImage) {
        if (Board.columns() != BuiltColumns || Board.rows() != BuiltRows) {
            UpdateStaticScene(Board);
            BuiltColumns = Board.columns();
            BuiltRows = Board.rows();
        }
        SubmitCellImage(Board);
        FrameCamera(Board, Layout, ViewportSize);
        return;
    }
    const bool bLayoutChanged = Board.columns() != BuiltColumns || Board.rows() != BuiltRows ||
                                static_cast<int32>(Board.bricks().size()) != BuiltBrickCount;
    if (bLayoutChanged) {
        if (Board.columns() != BuiltColumns || Board.rows() != BuiltRows) {
            UpdateStaticScene(Board);
            BuiltColumns = Board.columns();
            BuiltRows = Board.rows();
        }
        BuiltBrickCount = static_cast<int32>(Board.bricks().size());
        RebuildBricks(Board);
        BuiltRevision = Board.revision();
        LastBrickRebuild = Now;
    } else {
        const bool bRevisionChanged = Board.revision() != BuiltRevision;
        // Incremental updates touch only dirty bricks, so we update every frame without a 30 Hz throttle.
        if (bRevisionChanged || bBricksAnimating) {
            UpdateDirtyBricks(Board);
            BuiltRevision = Board.revision();
            LastBrickRebuild = Now;
        }
    }

    FrameCamera(Board, Layout, ViewportSize);
}

void FBallsBoardView::RebuildBricks(const BrickBoard& Board) {
    TArray<FTransform> Instances[ballColorCount];
    bBricksAnimating = false;
    const std::vector<Brick>& Bricks = Board.bricks();
    const int32 BrickCount = static_cast<int32>(Bricks.size());
    CachedBricks.Empty(BrickCount);
    CachedBricks.SetNum(BrickCount);

    TArray<FTransform> CellTransforms;
    for (int32 Index = 0; Index < BrickCount; ++Index) {
        const Brick& Piece = Bricks[Index];
        const int32 Color = toIndex(Piece.color);
        const int32 FirstInstance = Instances[Color].Num();
        const int32 CellCount = static_cast<int32>(Piece.cells.size());
        CachedBricks[Index] = FCachedBrick{Color, FirstInstance, CellCount, MakeSignature(Piece)};

        if (Piece.isAlive()) {
            bBricksAnimating |= Piece.riseOffset != 0.0f || Piece.hitFlash > 0.0f;
        }
        BuildBrickTransforms(Board, Index, Piece, CellTransforms);
        Instances[Color].Append(CellTransforms);
    }

    int32 TotalInstances = 0;
    for (int32 Color = 0; Color < ballColorCount; ++Color) {
        UInstancedStaticMeshComponent* Mesh = BrickMeshes[Color].Get();
        Mesh->ClearInstances();
        if (!Instances[Color].IsEmpty()) {
            Mesh->AddInstances(Instances[Color], false, false, false);
            // Instances added after registration do not refresh the cached bounds, so the component would be
            // culled with a zero-size box.
            Mesh->UpdateBounds();
            TotalInstances += Instances[Color].Num();
        }
    }
    static bool bLoggedBricks = false;
    if (!bLoggedBricks) {
        UInstancedStaticMeshComponent* First = BrickMeshes[0].Get();
        const FBox Bounds = First->Bounds.GetBox();
        UE_LOG(LogTemp, Verbose,
               TEXT("BallsBoardView: revision %llu, %d bricks, %d instances; mesh %s, material %s, visible %d, "
                    "actor hidden %d, bounds %s"),
               static_cast<unsigned long long>(Board.revision()), BrickCount, TotalInstances,
               First->GetStaticMesh() ? TEXT("ok") : TEXT("null"),
               First->GetMaterial(0) ? TEXT("ok") : TEXT("null"), First->IsVisible(), BoardActor->IsHidden(),
               *Bounds.ToString());
        bLoggedBricks = true;
    }
}

void FBallsBoardView::UpdateDirtyBricks(const BrickBoard& Board) {
    const std::vector<Brick>& Bricks = Board.bricks();
    const int32 BrickCount = static_cast<int32>(Bricks.size());
    if (BrickCount != CachedBricks.Num()) {
        RebuildBricks(Board);
        return;
    }

    bBricksAnimating = false;
    TBitArray<> DirtyFlags(false, BrickCount);
    TArray<int32> DirtyBrickIndices;
    DirtyBrickIndices.Reserve(64);

    for (int32 Index = 0; Index < BrickCount; ++Index) {
        const Brick& Piece = Bricks[Index];
        if (Piece.isAlive()) {
            bBricksAnimating |= Piece.riseOffset != 0.0f || Piece.hitFlash > 0.0f;
        }

        const FBrickSignature CurrentSig = MakeSignature(Piece);
        const FCachedBrick& Cached = CachedBricks[Index];

        if (CurrentSig != Cached.Signature) {
            if (!DirtyFlags[Index]) {
                DirtyFlags[Index] = true;
                DirtyBrickIndices.Add(Index);
            }

            // When a brick dies, also recompute its neighbours (the bricks at the 4 cells adjacent
            // to each of its cells, via Board.brickAt(column, row)), since OpenEdges insets depend on neighbours.
            if (Cached.Signature.bAlive && !CurrentSig.bAlive) {
                for (const BrickCell& Cell : Piece.cells) {
                    const int32 NeighborCoords[4][2] = {
                        {Cell.column - 1, Cell.row},
                        {Cell.column + 1, Cell.row},
                        {Cell.column, Cell.row - 1},
                        {Cell.column, Cell.row + 1}
                    };
                    for (const auto& [Col, Row] : NeighborCoords) {
                        if (Col >= 0 && Col < Board.columns() && Row >= 0 && Row < Board.rows()) {
                            const int32 NeighborIndex = Board.brickAt(Col, Row);
                            if (NeighborIndex >= 0 && NeighborIndex < BrickCount && NeighborIndex != Index) {
                                if (!DirtyFlags[NeighborIndex]) {
                                    DirtyFlags[NeighborIndex] = true;
                                    DirtyBrickIndices.Add(NeighborIndex);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (DirtyBrickIndices.IsEmpty()) {
        return;
    }

    bool bMeshDirty[ballColorCount] = {false};
    TArray<FTransform> CellTransforms;

    for (int32 Index : DirtyBrickIndices) {
        const Brick& Piece = Bricks[Index];
        FCachedBrick& Cached = CachedBricks[Index];
        Cached.Signature = MakeSignature(Piece);

        BuildBrickTransforms(Board, Index, Piece, CellTransforms);
        if (!CellTransforms.IsEmpty()) {
            UInstancedStaticMeshComponent* Mesh = BrickMeshes[Cached.Color].Get();
            if (CellTransforms.Num() == 1) {
                Mesh->UpdateInstanceTransform(Cached.FirstInstance, CellTransforms[0], false, false);
            } else {
                Mesh->BatchUpdateInstancesTransforms(Cached.FirstInstance, CellTransforms, false, false);
            }
            bMeshDirty[Cached.Color] = true;
        }
    }

    for (int32 Color = 0; Color < ballColorCount; ++Color) {
        if (bMeshDirty[Color]) {
            BrickMeshes[Color]->MarkRenderStateDirty();
        }
    }
}

void FBallsBoardView::ShowCellImage(bool bShow) {
    if (bShow == bShowingCells) {
        return;
    }
    bShowingCells = bShow;
    for (TStrongObjectPtr<UInstancedStaticMeshComponent>& Mesh : BrickMeshes) {
        // Millions of hidden instances would still cost memory and scene updates, so they are dropped, and rebuilt
        // when the cells grow large enough for meshes again.
        Mesh->ClearInstances();
        Mesh->SetVisibility(!bShow);
    }
    CachedBricks.Empty();
    BuiltBrickCount = -1;
    BuiltRevision = ~0ull;
    if (!bShow) {
        BallRenderer.ClearCells();
        SentCellSerial = 0;
        SentTileRevisions.clear();
    }
}

void FBallsBoardView::SubmitCellImage(const BrickBoard& Board) {
    const BrickCellImage& Image = Board.cellImage();
    const std::vector<uint8_t>& Bytes = Image.tileBytes();
    const std::vector<uint64_t>& Revisions = Image.tileRevisions();
    if (Bytes.empty()) {
        return;
    }
    FBallsCellImageUpdate Update;
    if (Image.layoutSerial() != SentCellSerial || Revisions.size() != SentTileRevisions.size()) {
        Update.bReplace = true;
        Update.TotalBytes = static_cast<uint32>(Bytes.size());
        Update.Bytes.Append(Bytes.data(), static_cast<int32>(Bytes.size()));
        SentCellSerial = Image.layoutSerial();
        SentTileRevisions = Revisions;
    } else {
        // Neighbouring changed tiles go as one run: each tile is contiguous in the image.
        size_t Tile = 0;
        while (Tile < Revisions.size()) {
            if (Revisions[Tile] == SentTileRevisions[Tile]) {
                ++Tile;
                continue;
            }
            const size_t First = Tile;
            while (Tile < Revisions.size() && Revisions[Tile] != SentTileRevisions[Tile]) {
                SentTileRevisions[Tile] = Revisions[Tile];
                ++Tile;
            }
            const size_t Offset = First * BrickCellImage::cellsPerTile;
            const size_t Size = (Tile - First) * BrickCellImage::cellsPerTile;
            Update.Runs.Add({static_cast<uint32>(Offset), static_cast<uint32>(Size)});
            Update.Bytes.Append(Bytes.data() + Offset, static_cast<int32>(Size));
        }
        if (Update.Runs.IsEmpty()) {
            return;
        }
    }
    FBallsCellDrawData Draw;
    Draw.Columns = Image.columns();
    Draw.Rows = Image.rows();
    Draw.TilesAcross = Image.tilesAcross();
    Draw.Origin = FVector3f(BoardToWorld(0.0, 0.0, 0.0));
    Draw.AxisX = FVector3f(CellWorldSize, 0.0f, 0.0f);
    Draw.AxisY = FVector3f(0.0f, 0.0f, -CellWorldSize);
    for (int32 Color = 0; Color < ballColorCount; ++Color) {
        const FColor Normal = BallsPalette::Brick(ballColorFromIndex(Color));
        Draw.Palette[Color] = BallsPalette::Linear(Normal);
        Draw.Palette[Color + ballColorCount] = BallsPalette::Linear(BallsPalette::Shade(Normal, theme::brickHitFlash));
    }
    BallRenderer.SubmitCells(MoveTemp(Update), Draw);
}

void FBallsBoardView::UpdateStaticScene(const BrickBoard& Board) {
    const double Columns = Board.columns();
    const double Rows = Board.rows();
    // The play field is exactly Columns x Rows cells, where the balls bounce. When it does not reach the screen
    // edge (portrait), the walls and a different outside color show where it ends. The camera shows far more
    // than the board on small levels, so the outside is a large fixed field to avoid black gutters.
    constexpr double OutsideField = 400.0;
    constexpr double WallThickness = 0.35;
    constexpr double WallFront = 0.25;
    constexpr double WallDepth = WallFront + BrickDepthCells + 0.05;
    Backdrop->SetRelativeLocationAndRotation(BoardToWorld(Columns * 0.5, Rows * 0.5, -BrickDepthCells - 0.05),
                                             FRotator::ZeroRotator);
    Backdrop->SetRelativeScale3D(FVector(Columns, 0.1, Rows));
    Outside->SetRelativeLocationAndRotation(BoardToWorld(Columns * 0.5, Rows * 0.5, -BrickDepthCells - 0.2),
                                            FRotator::ZeroRotator);
    Outside->SetRelativeScale3D(FVector(OutsideField, 0.1, OutsideField));

    // Walls sit just outside the field so the playable area stays exactly Columns x Rows.
    struct FWallSpec {
        double CenterColumn;
        double CenterRow;
        double Width;
        double Height;
    };
    const double WallsDepthCenter = WallFront - WallDepth * 0.5;
    const FWallSpec Specs[4] = {
        {-WallThickness * 0.5, Rows * 0.5, WallThickness, Rows + WallThickness * 2.0},
        {Columns + WallThickness * 0.5, Rows * 0.5, WallThickness, Rows + WallThickness * 2.0},
        {Columns * 0.5, -WallThickness * 0.5, Columns, WallThickness},
        {Columns * 0.5, Rows + WallThickness * 0.5, Columns, WallThickness},
    };
    for (int32 Index = 0; Index < 4; ++Index) {
        Walls[Index]->SetRelativeLocationAndRotation(
            BoardToWorld(Specs[Index].CenterColumn, Specs[Index].CenterRow, WallsDepthCenter), FRotator::ZeroRotator);
        Walls[Index]->SetRelativeScale3D(FVector(Specs[Index].Width, WallDepth, Specs[Index].Height));
    }
}

void FBallsBoardView::FrameCamera(const BrickBoard& Board, const FBallsGameplayLayout& Layout,
                                  FVector2D ViewportSize) {
    if (ViewportSize.X <= 1.0 || ViewportSize.Y <= 1.0) {
        return;
    }
    // Without this the camera keeps its authored 16:9 aspect, which crops the board in portrait.
    if (UCameraComponent* CameraComponent = Camera->GetCameraComponent()) {
        CameraComponent->SetConstraintAspectRatio(true);
        CameraComponent->SetAspectRatio(static_cast<float>(ViewportSize.X / FMath::Max(ViewportSize.Y, 1.0)));
    }
    // The camera looks straight at the board, so screen position is linear in board position on the Y = 0 plane.
    const double BoardWidthWorld = Board.columns() * CellWorldSize;
    const double WidthFraction = Layout.Board.GetSize().X / ViewportSize.X;
    const double HalfFov = FMath::DegreesToRadians(CameraFieldOfView * 0.5);
    const double VisibleWidth = BoardWidthWorld / FMath::Max(WidthFraction, 1e-3);
    const double VisibleHeight = VisibleWidth * ViewportSize.Y / ViewportSize.X;
    const double Distance = VisibleWidth * 0.5 / FMath::Tan(HalfFov);

    const FVector2D BoardCenter = Layout.Board.GetCenter();
    const double OffsetX = BoardCenter.X / ViewportSize.X - 0.5;
    const double OffsetY = BoardCenter.Y / ViewportSize.Y - 0.5;
    const FVector Center = BoardToWorld(Board.columns() * 0.5, Board.rows() * 0.5, 0.0);
    const FVector Location(Center.X - OffsetX * VisibleWidth, Distance, Center.Z + OffsetY * VisibleHeight);
    Camera->SetActorLocationAndRotation(Location, FRotator(0.0, -90.0, 0.0));

    static FVector2D LastViewport(-1.0, -1.0);
    if (LastViewport != ViewportSize) {
        const APlayerController* Player = World->GetFirstPlayerController();
        const AActor* ViewTarget = Player ? Player->GetViewTarget() : nullptr;
        UE_LOG(LogTemp, Verbose,
               TEXT("BallsBoardView: viewport %.0fx%.0f portrait %d board %dx%d rect %.0fx%.0f at %.0f,%.0f cell %.1f "
                    "dist %.0f loc %.0f,%.0f,%.0f rot %s, camera active %d, view target %s"),
               ViewportSize.X, ViewportSize.Y, Layout.bPortrait ? 1 : 0, Board.columns(), Board.rows(),
               Layout.Board.GetSize().X, Layout.Board.GetSize().Y, Layout.Board.Left, Layout.Board.Top,
               Layout.CellSize, Distance, Location.X, Location.Y, Location.Z, *Camera->GetActorRotation().ToString(),
               Camera->GetCameraComponent()->IsActive(), ViewTarget ? *ViewTarget->GetName() : TEXT("none"));
        LastViewport = ViewportSize;
    }
}

// A screen-space visibility grid: slots about 1.6 ball radii wide, so at most one ball is drawn per slot. At millions
// of balls the eye cannot see more than that, and the instance count collapses from the ball count to the number of
// slots (the same trick the SDL frontend uses).
float FBallsBoardView::VisibilitySlotCells(float BallRadius, const FBallsGameplayLayout& Layout, float DpiScale) {
    const float CellPixels = Layout.CellSize * DpiScale;
    return visibilitySlotSize(BallRadius, CellPixels);
}

void FBallsBoardView::SubmitBalls(Simulation& Sim, const FBallsGameplayLayout& Layout, float DpiScale) {
    const float SlotCells = VisibilitySlotCells(Sim.tuning().ballRadius, Layout, DpiScale);
    const BrickBoard& Board = Sim.board();
    Sim.backend().setVisibilityGrid(
        FMath::CeilToInt(static_cast<float>(Board.columns()) / SlotCells),
        FMath::CeilToInt(static_cast<float>(Board.rows()) / SlotCells));
    SubmitBalls(Sim.backend().snapshot(), Sim.tuning().ballRadius, Layout, DpiScale);
}

void FBallsBoardView::SubmitBalls(const BallSnapshot& Snapshot, float BallRadius, const FBallsGameplayLayout& Layout,
                                  float DpiScale) {
    const float CellPixels = Layout.CellSize * DpiScale;
    static bool bLoggedBalls = false;
    if (!bLoggedBalls) {
        UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: snapshot %llu balls"), static_cast<unsigned long long>(Snapshot.count));
        bLoggedBalls = true;
    }
    if (Snapshot.count == 0 || Snapshot.packed == nullptr) {
        BallRenderer.Clear();
        return;
    }
    FBallsDrawData Data;
    if (Snapshot.visibilityGrid) {
        // A grid snapshot has one word per slot and marks an unfilled slot with a sentinel that would decode as a ball
        // in the bottom-right corner; at millions of balls the thousands of empty slots piled up there as a purple
        // blob. Draw only the filled slots.
        Data.Packed.Reserve(static_cast<int32>(FMath::Min<size_t>(Snapshot.count, MaximumDrawnBalls)));
        for (size_t Slot = 0; Slot < Snapshot.count; ++Slot) {
            if (Snapshot.packed[Slot] != EmptyRenderSlot) {
                Data.Packed.Add(Snapshot.packed[Slot]);
            }
        }
    } else {
        // The backend did not return a grid (the first frame, or a backend without one): never copy or draw more
        // balls than the screen can show; an even stride keeps the picture representative.
        const size_t Drawn = FMath::Min<size_t>(Snapshot.count, MaximumDrawnBalls);
        const size_t Stride = Snapshot.count / Drawn;
        Data.Packed.SetNumUninitialized(static_cast<int32>(Drawn));
        if (Stride <= 1) {
            FMemory::Memcpy(Data.Packed.GetData(), Snapshot.packed, Drawn * sizeof(uint32));
        } else {
            for (size_t Index = 0; Index < Drawn; ++Index) {
                Data.Packed[static_cast<int32>(Index)] = Snapshot.packed[Index * Stride];
            }
        }
    }
    if (Data.Packed.IsEmpty()) {
        BallRenderer.Clear();
        return;
    }
    Data.UnpackScale = FVector2f(1.0f / renderPackScale(Snapshot.fieldWidth), 1.0f / renderPackScale(Snapshot.fieldHeight));
    Data.Radius = FMath::Max(BallRadius, MinimumBallPixels / FMath::Max(CellPixels, 1e-3f));
    Data.Brightness = BallBrightness;
    static float LastRadius = -1.0f;
    if (!FMath::IsNearlyEqual(LastRadius, Data.Radius, 1e-5f)) {
        UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: radius %.4f cell %.2f dpi %.2f count %llu"), Data.Radius,
               Layout.CellSize, DpiScale, static_cast<unsigned long long>(Snapshot.count));
        LastRadius = Data.Radius;
    }
    Data.Origin = FVector3f(BoardToWorld(0.0, 0.0, 0.0));
    Data.AxisX = FVector3f(CellWorldSize, 0.0f, 0.0f);
    Data.AxisY = FVector3f(0.0f, 0.0f, -CellWorldSize);
    for (int32 Color = 0; Color < 8; ++Color) {
        Data.Palette[Color] = BallsPalette::Linear(BallsPalette::Ball(Color));
    }
    BallRenderer.Submit(MoveTemp(Data));
}
