#include "BallsControlServer.h"

#include "BallsApp.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Modules/ModuleManager.h"

namespace {

const TCHAR* const JsonContentType = TEXT("application/json");

FString EnsureStartSlash(const FString& Path) {
    return Path.StartsWith(TEXT("/")) ? Path : FString(TEXT("/")) + Path;
}

}

FBallsControlServer::FBallsControlServer(FBallsApp& InApp) : App(InApp) {}

FBallsControlServer::~FBallsControlServer() { Stop(); }

bool FBallsControlServer::Start(int32 Port) {
    if (bRunning) {
        return true;
    }
    if (Port <= 0) {
        return false;
    }
    // The module is not loaded by anything else in this game, so load it rather than only asking whether it is up.
    FHttpServerModule& Module = FModuleManager::LoadModuleChecked<FHttpServerModule>(TEXT("HTTPServer"));
    Router = Module.GetHttpRouter(static_cast<uint32>(Port), true);
    if (!Router.IsValid()) {
        UE_LOG(LogTemp, Warning, TEXT("BallsControl: port %d could not be bound"), Port);
        return false;
    }
    // UE's router binds paths one by one: every path the core answers, then Unreal's own.
    for (const std::string_view Path : controlApiPaths()) {
        Bind(*FString(UTF8_TO_TCHAR(std::string(Path).c_str())));
    }
    for (const FString& Path : FBallsApp::OwnControlPaths()) {
        Bind(*Path);
    }
    Module.StartAllListeners();
    BoundPort = Port;
    bRunning = true;
    UE_LOG(LogTemp, Display, TEXT("BallsControl: listening on port %d"), Port);
    return true;
}

void FBallsControlServer::Bind(const TCHAR* Path) {
    const FString Route(Path);
    FHttpRequestHandler Handler;
    Handler.BindLambda([this, Route](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
        Queue(Route, Request.QueryParams, OnComplete);
        return true;
    });
    Routes.Add(Router->BindRoute(FHttpPath(Route), EHttpServerRequestVerbs::VERB_GET, Handler));
}

void FBallsControlServer::Queue(const FString& Path, const TMap<FString, FString>& Query,
                                const FHttpResultCallback& OnComplete) {
    // The answer is produced on the game thread, where the screens live; the task only carries copies.
    FBallsApp* AppPtr = &App;
    TMap<FString, FString> Params = Query;
    AsyncTask(ENamedThreads::GameThread, [AppPtr, Path, Params, OnComplete]() {
        FString Body;
        int32 Code = 200;
        AppPtr->HandleControl(EnsureStartSlash(Path), Params, Body, Code);
        TUniquePtr<FHttpServerResponse> Response =
            Code == 200 ? FHttpServerResponse::Create(Body, JsonContentType)
                        : FHttpServerResponse::Error(static_cast<EHttpServerResponseCodes>(Code), TEXT("error"), Body);
        OnComplete(MoveTemp(Response));
    });
}

void FBallsControlServer::Stop() {
    if (!bRunning) {
        return;
    }
    if (Router.IsValid()) {
        for (const FHttpRouteHandle& Route : Routes) {
            Router->UnbindRoute(Route);
        }
    }
    Routes.Reset();
    Router.Reset();
    if (FHttpServerModule::IsAvailable()) {
        FHttpServerModule::Get().StopAllListeners();
    }
    bRunning = false;
}
