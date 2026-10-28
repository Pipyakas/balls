#pragma once

#include "BallsDrawData.h"
#include "CoreMinimal.h"
#include "SceneViewExtension.h"

#include <atomic>

class FBallsSceneViewExtension final : public FSceneViewExtensionBase {
public:
    explicit FBallsSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

    void SetHasBalls(bool bInHasBalls) { bHasBalls.store(bInHasBalls, std::memory_order_relaxed); }
    void SetDrawData_RenderThread(TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> Data) {
        DrawData = MoveTemp(Data);
    }

    void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
                                       FPostProcessingPassDelegateArray& InOutPassCallbacks,
                                       bool bIsPassEnabled) override;

protected:
    bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override {
        return bHasBalls.load(std::memory_order_relaxed);
    }

private:
    FScreenPassTexture DrawBalls_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
                                              const FPostProcessMaterialInputs& Inputs);

    std::atomic<bool> bHasBalls = false;
    TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> DrawData;
};
