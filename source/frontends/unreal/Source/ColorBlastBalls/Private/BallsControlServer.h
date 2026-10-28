#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"

class FBallsApp;
class IHttpRouter;

// The game's HTTP control API ("Launch options and automation"). Requests arrive on the HTTP listener thread, so
// each one is queued onto the game thread and answered from there; the response callback is safe to call late.
class FBallsControlServer {
public:
    explicit FBallsControlServer(FBallsApp& InApp);
    ~FBallsControlServer();

    // False when the HTTPServer module is missing or the port cannot be bound, so the caller can log and go on.
    bool Start(int32 Port);
    void Stop();
    bool IsRunning() const { return bRunning; }
    int32 GetPort() const { return BoundPort; }

private:
    void Bind(const TCHAR* Path);
    void Queue(const FString& Path, const TMap<FString, FString>& Query, const FHttpResultCallback& OnComplete);

    FBallsApp& App;
    TSharedPtr<IHttpRouter> Router;
    TArray<FHttpRouteHandle> Routes;
    int32 BoundPort = 0;
    bool bRunning = false;
};
