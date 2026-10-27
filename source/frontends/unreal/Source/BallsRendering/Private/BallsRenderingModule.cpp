#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FBallsRenderingModule final : public IModuleInterface {
public:
    void StartupModule() override {
        const FString ShaderDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
        AddShaderSourceDirectoryMapping(TEXT("/Balls"), ShaderDirectory);
    }
};

IMPLEMENT_MODULE(FBallsRenderingModule, BallsRendering)
