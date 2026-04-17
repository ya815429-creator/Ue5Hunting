#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"

// 定义发现服务器后的回调函数
typedef TFunction<void(FString)> FOnServerFoundCallback;

class FServerDiscoveryWorker : public FRunnable
{
public:
    FServerDiscoveryWorker(bool bInIsServer, FOnServerFoundCallback InCallback);
    virtual ~FServerDiscoveryWorker();

    virtual uint32 Run() override;
    virtual void Stop() override;

private:
    FSocket* Socket;
    bool bIsRunning;
    bool bIsServer;
    int32 Port_;
    FOnServerFoundCallback Callback;                                                                // 发现服务器后的回调

    void RunServer();                                                                               // 广播逻辑
    void RunClient();                                                                               // 监听逻辑
};