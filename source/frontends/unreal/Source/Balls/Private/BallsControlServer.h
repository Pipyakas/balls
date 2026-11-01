#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"

class FBallsApp;
class IHttpRouter;

class FBallsControlServer {
public:
    explicit FBallsControlServer(FBallsApp& InApp);
    ~FBallsControlServer();

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
