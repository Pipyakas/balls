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
const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

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
    CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
    CameraComponent->PostProcessSettings.AutoExposureMethod = AEM_Histogram;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    CameraComponent->PostProcessSettings.AutoExposureMinBrightness = 0.01f;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    CameraComponent->PostProcessSettings.AutoExposureMaxBrightness = 8.0f;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
    CameraComponent->PostProcessSettings.AutoExposureBias = 0.0f;
    CameraComponent->PostProcessBlendWeight = 1.0f;
    Camera.Reset(CameraActor);
    SceneActors.Emplace(CameraActor);

    ADirectionalLight* KeyLight = World->SpawnActor<ADirectionalLight>(
        FVector::ZeroVector, FRotator(-35.0, -60.0, 0.0), Parameters);
    // Runtime-spawned lights default to Stationary, which does not light dynamic meshes without baked data.
    KeyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    KeyLight->GetLightComponent()->SetIntensity(1.2f);
    // The board is a flat 2.5D scene; the backdrop sits right behind the bricks, so shadows only add streaks.
    KeyLight->GetLightComponent()->SetCastShadows(false);
    SceneActors.Emplace(KeyLight);

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
    UE_LOG(LogTemp, Verbose, TEXT("BallsBoardView: cube mesh %s"), Cube ? TEXT("loaded") : TEXT("MISSING"));

    UStaticMeshComponent* BackdropComponent = NewObject<UStaticMeshComponent>(Actor, TEXT("Backdrop"));
    BackdropComponent->SetStaticMesh(Cube);
    BackdropComponent->SetMobility(EComponentMobility::Movable);
    BackdropComponent->SetMaterial(0, MakeColoredMaterial(BackdropComponent, BallsPalette::BackgroundDeep, 0.9f));
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
        Bricks->SetStaticMesh(Cube);
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

void FBallsBoardView::ApplyGraphics(EBallsGraphics Quality) {
    // With bloom off the balls are drawn at their palette colour instead of above-white, so nothing clips.
    BallBrightness = Quality == EBallsGraphics::High ? 2.2f : 1.0f;
    UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
    if (!CameraComponent) {
        return;
    }
    FPostProcessSettings& Post = CameraComponent->PostProcessSettings;
    Post.bOverride_BloomIntensity = true;
    Post.BloomIntensity = Quality == EBallsGraphics::High ? 0.9f : Quality == EBallsGraphics::Balanced ? 0.5f : 0.0f;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = Quality == EBallsGraphics::High ? 0.25f : Quality == EBallsGraphics::Balanced ? 0.15f : 0.0f;
    // Auto-exposure stays on the histogram at every level: a manual exposure blacked the frame out, and the pass is
    // only a fraction of what the anti-aliasing and screen-percentage switches save.
}

void FBallsBoardView::Hide() {
    if (bVisible) {
        BoardActor->SetActorHiddenInGame(true);
        BallRenderer.Clear();
        BuiltRevision = ~0ull;
        bVisible = false;
    }
}

void FBallsBoardView::Update(const Simulation& Sim, const FBallsGameplayLayout& Layout, FVector2D ViewportSize,
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
    const BrickBoard& Board = Sim.board();
    // A rebuild recreates every instance buffer, so an animation (hit flash, rising bricks) steps at 30 Hz instead
    // of every frame; a changed board still rebuilds at once.
    const double Now = FPlatformTime::Seconds();
    const bool bRevisionChanged = Board.revision() != BuiltRevision;
    if (bRevisionChanged || (bBricksAnimating && Now - LastBrickRebuild >= BrickAnimationInterval)) {
        RebuildBricks(Board);
        BuiltRevision = Board.revision();
        LastBrickRebuild = Now;
    }
    if (Board.columns() != BuiltColumns || Board.rows() != BuiltRows) {
        UpdateStaticScene(Board);
        BuiltColumns = Board.columns();
        BuiltRows = Board.rows();
    }
    FrameCamera(Board, Layout, ViewportSize);
}

void FBallsBoardView::RebuildBricks(const BrickBoard& Board) {
    TArray<FTransform> Instances[ballColorCount];
    bBricksAnimating = false;
    const std::vector<Brick>& Bricks = Board.bricks();
    for (int32 Index = 0; Index < static_cast<int32>(Bricks.size()); ++Index) {
        const Brick& Piece = Bricks[Index];
        if (!Piece.isAlive()) {
            continue;
        }
        bBricksAnimating |= Piece.riseOffset != 0.0f || Piece.hitFlash > 0.0f;
        const double Front = BrickFlashPopCells * Piece.hitFlash;
        const double Back = -BrickDepthCells;
        for (const BrickCell& Cell : Piece.cells) {
            const FCellEdges Edges = OpenEdges(Board, Index, Cell);
            const double Left = Cell.column + (Edges.bLeft ? CellInset : 0.0);
            const double Right = Cell.column + 1.0 - (Edges.bRight ? CellInset : 0.0);
            const double Top = Cell.row + Piece.riseOffset + (Edges.bTop ? CellInset : 0.0);
            const double Bottom = Cell.row + 1.0 + Piece.riseOffset - (Edges.bBottom ? CellInset : 0.0);
            const FVector Center = BoardToWorld((Left + Right) * 0.5, (Top + Bottom) * 0.5, (Front + Back) * 0.5);
            Instances[toIndex(Piece.color)].Emplace(FQuat::Identity, Center,
                                                     FVector(Right - Left, Front - Back, Bottom - Top));
        }
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
               static_cast<unsigned long long>(Board.revision()), static_cast<int32>(Bricks.size()), TotalInstances,
               First->GetStaticMesh() ? TEXT("ok") : TEXT("null"),
               First->GetMaterial(0) ? TEXT("ok") : TEXT("null"), First->IsVisible(), BoardActor->IsHidden(),
               *Bounds.ToString());
        bLoggedBricks = true;
    }
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

void FBallsBoardView::SubmitBalls(Simulation& Sim, const FBallsGameplayLayout& Layout, float DpiScale) {
    // Ask the backend for a screen-space visibility grid first: slots about 1.6 ball radii wide, so at most one
    // ball is drawn per slot. At millions of balls the eye cannot see more than that, and the instance count
    // collapses from the ball count to the number of slots (the same trick the SDL frontend uses).
    const float CellPixels = Layout.CellSize * DpiScale;
    const float SlotCells =
        FMath::Max(Sim.tuning().ballRadius * VisibilitySlotPerRadius, 1.0f / FMath::Max(CellPixels, 1.0f));
    const BrickBoard& Board = Sim.board();
    Sim.backend().setVisibilityGrid(
        FMath::CeilToInt(static_cast<float>(Board.columns()) / SlotCells),
        FMath::CeilToInt(static_cast<float>(Board.rows()) / SlotCells));
    const BallSnapshot Snapshot = Sim.backend().snapshot();
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
    Data.Radius = FMath::Max(Sim.tuning().ballRadius, MinimumBallPixels / FMath::Max(CellPixels, 1e-3f));
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
