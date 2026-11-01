#pragma once

#include "BallsDrawData.h"
#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

#include <atomic>

class FBallsSceneViewExtension final : public FSceneViewExtensionBase {
public:
    explicit FBallsSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

    void SetHasBalls(bool bInHasBalls) { bHasBalls.store(bInHasBalls, std::memory_order_relaxed); }
    void SetDrawData_RenderThread(TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> Data) {
        DrawData = MoveTemp(Data);
    }
    void SetHasCells(bool bInHasCells) { bHasCells.store(bInHasCells, std::memory_order_relaxed); }
    void QueueCells_RenderThread(TSharedPtr<const FBallsCellImageUpdate, ESPMode::ThreadSafe> Update,
                                 const FBallsCellDrawData& Draw) {
        if (Update->bReplace) {
            PendingCellUpdates.Reset();
        }
        PendingCellUpdates.Add(MoveTemp(Update));
        CellDraw = Draw;
    }
    void ClearCells_RenderThread() {
        PendingCellUpdates.Reset();
        CellBuffer.SafeRelease();
    }

    void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
                                       FPostProcessingPassDelegateArray& InOutPassCallbacks,
                                       bool bIsPassEnabled) override;

protected:
    bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override {
        return bHasBalls.load(std::memory_order_relaxed) || bHasCells.load(std::memory_order_relaxed);
    }

private:
    FScreenPassTexture DrawBalls_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
                                              const FPostProcessMaterialInputs& Inputs);
    void UploadCells_RenderThread(FRDGBuilder& GraphBuilder);
    void DrawCells_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassRenderTarget& Output,
                                float OutputGamma);

    std::atomic<bool> bHasBalls = false;
    std::atomic<bool> bHasCells = false;

    TRefCountPtr<FRDGPooledBuffer> CellBuffer;
    TArray<TSharedPtr<const FBallsCellImageUpdate, ESPMode::ThreadSafe>> PendingCellUpdates;
    FBallsCellDrawData CellDraw;
    TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> DrawData;
};
