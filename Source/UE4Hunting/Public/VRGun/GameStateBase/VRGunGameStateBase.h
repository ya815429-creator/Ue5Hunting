#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "VRGunGameStateBase.generated.h"

/**
 * VR 射击游戏全局状态机 (GameState)
 * 运行于双端网络。作为网络同步枢纽，负责将服务器权威的倒计时、当前播放的定序器资产，以及绝对的序列进度时间下发给所有客户端。
 */
UCLASS()
class UE4HUNTING_API AVRGunGameStateBase : public AGameStateBase
{
    GENERATED_BODY()

#pragma region /* 初始化与状态播报 */
public:
    AVRGunGameStateBase();
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    /** 同步变量：开局倒计时，供客户端 UMG 展现 */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Logic")
    int32 MatchStartCountdown = 5;
#pragma endregion


#pragma region /* 定序器管理与弹性网络纠偏 */
public:

    // 定义一个全网同步的速率变量，只要它改变，客户端就会自动触发 OnRep 函数
    UPROPERTY(ReplicatedUsing = OnRep_GlobalSequencePlayRate)
    float GlobalSequencePlayRate = 1.0f;

    UFUNCTION()
    void OnRep_GlobalSequencePlayRate();

    UFUNCTION(NetMulticast,Reliable,BlueprintCallable)
    void Multicast_GlobalSequencePlayRate(float NewRate);

    // 供蓝图调用的普通函数（只在服务器调用）
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GameState|Sequence")
    void SetGlobalSequencePlayRate(float NewRate);

    /** * 服务器统筹调度接口：切换至新的定序器动画。
     * 调用后会更新同步变量，从而通知全网执行新序列的加载与实体绑定。
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GameState|Sequence")
    void Server_SwitchSequence(class ULevelSequence* NewAsset);

    /** 当前正在被全网要求播放的关卡序列资产 */
    UPROPERTY(ReplicatedUsing = OnRep_ActiveSequenceData, BlueprintReadOnly, Category = "GameState|Sequence")
    class ULevelSequence* ActiveSequenceAsset;

    /** * 全网统一的延迟起跑时间戳 (基于 ServerWorldTimeSeconds)。
     * 给定一个略晚于当前的未来时间，抵消网络延迟的影响，让客户端做好同步发车的准备。
     */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Sequence")
    float ActiveSequenceStartTime;

    /** * 进度同步灯塔：服务器当前定序器的真实播放进度。
     * 完美包含并计算了服务器上可能存在的卡顿或时慢指令。客户端将不断比对该时间进行弹性容差纠偏。
     */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Sequence")
    float ServerSequencePosition;

    /** * 纯本地脱机播放器容器指针。
     * 各端各自生成，完全隔离 UE 引擎底层的强制位置拉扯，从而避免网络条件差时的模型鬼畜。
     */
    UPROPERTY(BlueprintReadOnly, Category = "GameState|Sequence")
    class ALevelSequenceActor* LocalSequenceActor;

protected:
    /** RepNotify 回调：当客户端收到新序列指令时，执行本地 Actor 生成并分发绑定信号 */
    UFUNCTION()
    void OnRep_ActiveSequenceData();
#pragma endregion

#pragma region /* 投币系统全网同步 (驱动 3D UI) */
public:
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    bool Rep_bIsFreeMode = false;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    int32 GlobalCoinsPerCredit = 3;

    // ============ Player 0 数据 ============
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    int32 Rep_P0_Coins = 0; // 当前散币 (比如 2/3 里的 2)

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    int32 Rep_P0_Credits = 0; // 剩余信用点数 (可续玩的局数)

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    bool Rep_P0_IsPaid = false; // 是否已激活

    // ============ Player 1 数据 ============
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    int32 Rep_P1_Coins = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    int32 Rep_P1_Credits = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "GameState|Arcade")
    bool Rep_P1_IsPaid = false;

    // 仅供服务器 GameInstance 调用的数据灌入接口
    void UpdateCoinData(bool bFreeMode, int32 Cost, int32 P0_Coins, int32 P0_Credits, bool P0_Paid, int32 P1_Coins, int32 P1_Credits, bool P1_Paid);
#pragma endregion
};