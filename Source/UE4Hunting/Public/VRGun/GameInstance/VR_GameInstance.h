#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "VRGun/UDP/FServerDiscoveryWorker.h"
#include "VR_GameInstance.generated.h"

UCLASS()
class UE4HUNTING_API UVR_GameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;
    virtual void Shutdown() override;
    virtual void BeginDestroy() override; // 增加这行：对象的终极销毁回调
private:
    // 持有 Worker 指针和线程控制对象
    FServerDiscoveryWorker* DiscoveryWorker = nullptr;
    FRunnableThread* WorkerThread = nullptr;

    // 辅助函数：连接服务器
    void ConnectToFoundServer(FString IPAddress);

    // 防止客户端疯狂调用加入的防连发锁
    bool bIsConnectingToServer = false;
public:

    // 从 JSON 读出的真实身份
    UPROPERTY(BlueprintReadOnly, Category = "VR|Network")
    bool bIsHostServer = false;

    // 🌟 全局启动接口：供启动地图调用
    UFUNCTION(BlueprintCallable, Category = "VR|Network")
    void ExecuteStartupFlow();

    /** 跨关卡记录：期望连入的玩家总数（默认1，防独立测试出错） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Network")
    int32 ExpectedPlayerCount = 1;
#pragma region /* 街机投币与状态管理 */
public:
    /**展会/免费模式开关
     * 如果勾选，启动游戏时所有玩家自动视为已投币，且返回大厅也不会重置状态。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Arcade")
    bool bExhibitionFreeMode = false;

    /** 单局所需币数 (街机标准配置) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Arcade")
    int32 CoinsPerCredit = 3;

    UPROPERTY(BlueprintReadWrite, Category = "VR|Arcade")
    bool bPlayer0_Paid = false;

    UPROPERTY(BlueprintReadWrite, Category = "VR|Arcade")
    bool bPlayer1_Paid = false;


    // 真实的物理投币溢出池 (跨关卡永久保存)
    UPROPERTY(BlueprintReadWrite, Category = "VR|Arcade")
    int32 Player0_RawCoins = 0;

    UPROPERTY(BlueprintReadWrite, Category = "VR|Arcade")
    int32 Player1_RawCoins = 0;

    /** 接收硬件投币信号的接口 */
    UFUNCTION(BlueprintCallable, Category = "VR|Arcade")
    void InsertCoin(int32 PlayerIndex);

    /** 游戏结束返回大厅时调用，用于重置投币状态 */
    UFUNCTION(BlueprintCallable, Category = "VR|Arcade")
    void ResetCoinStatus();

    /** 供 GameState 在关卡刚加载时主动拉取数据的接口 */
    void SyncCoinDataToGameState();
#pragma endregion
};