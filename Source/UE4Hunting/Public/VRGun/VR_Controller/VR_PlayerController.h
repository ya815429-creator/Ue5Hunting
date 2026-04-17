#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VR_PlayerController.generated.h"

class AQteBase_New;

UCLASS()
class UE4HUNTING_API AVR_PlayerController : public APlayerController
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
    /** 覆写生命周期结束函数，用于清理残留的 VR 渲染层 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
public:
    // 启动服务器
    UFUNCTION(BlueprintCallable, Category = "Networking")
    void StartListenServer(FString MapName);

    // 连接服务器
    UFUNCTION(BlueprintCallable, Category = "Networking")
    void JoinServer(FString IPAddress);

#pragma region /* 同步QTE生命周期 */
    UFUNCTION(Server, Reliable)
    void Server_ReportQTEReady(AQteBase_New* ReadyQTE);

#pragma endregion

#pragma region /* VR 无缝加载与流送握手 */
public:

    /** 客户端 RPC：命令客户端黑屏*/
    UFUNCTION(Client, Reliable, BlueprintCallable, Category = "VR|Loading")
    void Client_ShowLoadingScreen(float Duration);

    /** 客户端 RPC：命令客户端正式亮起画面 */
    UFUNCTION(Client, Reliable, BlueprintCallable, Category = "VR|Loading")
    void Client_HideLoadingScreen();

    /** 服务器 RPC：客户端本地的流送关卡加载完毕后，主动向服务器汇报就绪 */
    UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "VR|Loading")
    void Server_NotifyLevelLoaded();

    /** 客户端 RPC：接收需要加载和卸载的关卡名单数组，全自动并发流送系统 */
    UFUNCTION(Client, Reliable, Category = "VR|Streaming")
    void Client_ExecuteLevelTransition(const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload);

    /** C++ 内部回调：每当一个流送关卡加载完，底层会自动调用它 */
    UFUNCTION()
    void OnSingleStreamLevelLoaded();

    /** 客户端 RPC：同步修改本地定序器的播放速率 (用于流送时暂停动画) */
    UFUNCTION(Client, Reliable, Category = "VR|Streaming")
    void Client_SetSequencePlayRate(float PlayRate);
protected:
    //延迟流送缓存与执行函数
    /** 缓存并发加载的名单，用于等待黑屏完全遮挡后再执行 */
    TArray<FName> PendingLevelsToLoad;
    TArray<FName> PendingLevelsToUnload;

    /** 定时器回调：等待黑屏彻底结束后的实际流送执行函数 */
    UFUNCTION()
    void DoExecuteLevelTransition();

    /** 并发加载计数器 */
    int32 TargetLoadCount = 0;
    int32 CurrentLoadedCount = 0;
   

#pragma endregion
};