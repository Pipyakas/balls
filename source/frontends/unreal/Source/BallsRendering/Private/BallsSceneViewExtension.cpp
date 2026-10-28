#include "BallsSceneViewExtension.h"

#include "BallsRenderer.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace {

// Profiling only: the ball pass is the largest single item in this frame, and these two switches split it into
// vertex work (draw the quads off-screen) and repeated draws (marginal cost of one more million-ball pass).
static TAutoConsoleVariable<int32> CVarBallsDrawPasses(
    TEXT("r.Balls.DrawPasses"), 1, TEXT("Draw the ball pass this many times (profiling)."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarBallsVertexOnly(
    TEXT("r.Balls.VertexOnly"), 0, TEXT("Cull every ball quad in the vertex shader (profiling)."),
    ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FBallsDrawParameters, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PackedBalls)
    SHADER_PARAMETER(FMatrix44f, BoardToClip)
    SHADER_PARAMETER(FVector2f, UnpackScale)
    SHADER_PARAMETER(float, Radius)
    SHADER_PARAMETER(float, Brightness)
    // 1 when drawing into HDR scene color (pre-exposed, linear); 0 over the tonemapped image.
    SHADER_PARAMETER(float, SceneReferred)
    // Exponent applied to the output; 1/2.2 when drawing into a non-sRGB display-referred target.
    SHADER_PARAMETER(float, OutputGamma)
    // Profiling switch: every quad goes off-screen, so the pass measures vertex work alone.
    SHADER_PARAMETER(float, VertexOnly)
    SHADER_PARAMETER_ARRAY(FVector4f, Palette, [8])
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FBallsVS final : public FGlobalShader {
public:
    DECLARE_GLOBAL_SHADER(FBallsVS);
    SHADER_USE_PARAMETER_STRUCT(FBallsVS, FGlobalShader);
    using FParameters = FBallsDrawParameters;
};

class FBallsPS final : public FGlobalShader {
public:
    DECLARE_GLOBAL_SHADER(FBallsPS);
    SHADER_USE_PARAMETER_STRUCT(FBallsPS, FGlobalShader);
    using FParameters = FBallsDrawParameters;
};

IMPLEMENT_GLOBAL_SHADER(FBallsVS, "/Balls/BallsDraw.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FBallsPS, "/Balls/BallsDraw.usf", "MainPS", SF_Pixel);

FMatrix44f BoardToClipMatrix(const FBallsDrawData& Data, const FSceneView& View) {
    const FMatrix BoardToWorld(FPlane(FVector(Data.AxisX), 0.0), FPlane(FVector(Data.AxisY), 0.0),
                               FPlane(0.0, 0.0, 0.0, 0.0), FPlane(FVector(Data.Origin), 1.0));
    const FViewMatrices& Matrices = View.ViewMatrices;
    return FMatrix44f(BoardToWorld * Matrices.GetWorldToView() * Matrices.GetViewToClipNoAA());
}

}

void FBallsSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
                                                             FPostProcessingPassDelegateArray& InOutPassCallbacks,
                                                             bool bIsPassEnabled) {
    // Drawing after motion blur keeps the balls in HDR scene color, so bloom and tonemapping apply to them.
    // The mobile renderer has no motion blur pass and never calls that subscription, so on mobile the balls are
    // drawn over the tonemapped image instead.
    const EPostProcessingPass TargetPass =
        View.GetFeatureLevel() <= ERHIFeatureLevel::ES3_1 ? EPostProcessingPass::Tonemap : EPostProcessingPass::MotionBlur;
    if (Pass == TargetPass) {
        InOutPassCallbacks.Add(
            FPostProcessingPassDelegate::CreateRaw(this, &FBallsSceneViewExtension::DrawBalls_RenderThread));
    }
}

FScreenPassTexture FBallsSceneViewExtension::DrawBalls_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
                                                                     const FPostProcessMaterialInputs& Inputs) {
    const FScreenPassTexture SceneColor =
        FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
    FScreenPassRenderTarget Output(SceneColor, ERenderTargetLoadAction::ELoad);
    if (Inputs.OverrideOutput.IsValid()) {
        AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), SceneColor, Inputs.OverrideOutput);
        Output = Inputs.OverrideOutput;
        Output.LoadAction = ERenderTargetLoadAction::ELoad;
    }

    const TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> Data = DrawData;
    if (!Data.IsValid() || Data->Packed.IsEmpty()) {
        return MoveTemp(Output);
    }

    // The upload reads straight from the frame's array, so keep it alive until the graph executes.
    GraphBuilder.AllocObject<TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe>>(Data);
    FRDGBufferRef PackedBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Balls.Packed"), sizeof(uint32),
                                                        Data->Packed.Num(), Data->Packed.GetData(),
                                                        Data->Packed.Num() * sizeof(uint32), ERDGInitialDataFlags::NoCopy);

    FBallsDrawParameters* Parameters = GraphBuilder.AllocParameters<FBallsDrawParameters>();
    Parameters->View = View.ViewUniformBuffer;
    Parameters->PackedBalls = GraphBuilder.CreateSRV(PackedBuffer);
    Parameters->BoardToClip = BoardToClipMatrix(*Data, View);
    Parameters->UnpackScale = Data->UnpackScale;
    Parameters->Radius = Data->Radius;
    const bool bMobile = View.GetFeatureLevel() <= ERHIFeatureLevel::ES3_1;
    const bool bSrgbTarget = EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB);
    Parameters->SceneReferred = bMobile ? 0.0f : 1.0f;
    // Over a tonemapped image there is no bloom to feed, so the balls only need to read as their palette colors.
    // Desktop uses the frame's brightness, which drops to 1 when the graphics level turns bloom off.
    Parameters->Brightness = bMobile ? 1.0f : Data->Brightness;
    Parameters->OutputGamma = bMobile && !bSrgbTarget ? 1.0f / 2.2f : 1.0f;
    Parameters->VertexOnly = CVarBallsVertexOnly.GetValueOnRenderThread() != 0 ? 1.0f : 0.0f;
    for (int32 Index = 0; Index < 8; ++Index) {
        Parameters->Palette[Index] = FVector4f(Data->Palette[Index]);
    }
    Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

    const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
    TShaderMapRef<FBallsVS> VertexShader(ShaderMap);
    TShaderMapRef<FBallsPS> PixelShader(ShaderMap);
    const FIntRect Viewport = Output.ViewRect;
    const uint32 BallCount = static_cast<uint32>(Data->Packed.Num());

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("Balls %u", BallCount), Parameters, ERDGPassFlags::Raster,
        [Parameters, VertexShader, PixelShader, Viewport, BallCount](FRHICommandList& RHICmdList) {
            RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

            FGraphicsPipelineStateInitializer Pipeline;
            RHICmdList.ApplyCachedRenderTargets(Pipeline);
            Pipeline.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add,
                                                    BF_Zero, BF_One>::GetRHI();
            Pipeline.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            Pipeline.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
            Pipeline.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            Pipeline.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            Pipeline.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            Pipeline.PrimitiveType = PT_TriangleList;
            SetGraphicsPipelineState(RHICmdList, Pipeline, 0);

            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *Parameters);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
            // One triangle per ball; see the corner table in BallsDraw.usf.
            const int32 Passes = FMath::Clamp(CVarBallsDrawPasses.GetValueOnRenderThread(), 1, 8);
            for (int32 Pass = 0; Pass < Passes; ++Pass) {
                RHICmdList.DrawPrimitive(0, 1, BallCount);
            }
        });

    return MoveTemp(Output);
}

FBallsRenderer::FBallsRenderer() : Extension(FSceneViewExtensions::NewExtension<FBallsSceneViewExtension>()) {}

FBallsRenderer::~FBallsRenderer() { Clear(); }

void FBallsRenderer::Submit(FBallsDrawData&& Data) {
    Extension->SetHasBalls(!Data.Packed.IsEmpty());
    TSharedPtr<const FBallsDrawData, ESPMode::ThreadSafe> Frame =
        MakeShared<FBallsDrawData, ESPMode::ThreadSafe>(MoveTemp(Data));
    ENQUEUE_RENDER_COMMAND(BallsSubmit)
    ([Extension = Extension, Frame = MoveTemp(Frame)](FRHICommandListImmediate&) mutable {
        Extension->SetDrawData_RenderThread(MoveTemp(Frame));
    });
}

void FBallsRenderer::Clear() {
    Extension->SetHasBalls(false);
    ENQUEUE_RENDER_COMMAND(BallsClear)
    ([Extension = Extension](FRHICommandListImmediate&) { Extension->SetDrawData_RenderThread(nullptr); });
}
