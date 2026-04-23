#pragma once

#include "CoreMinimal.h"
#include "CMyGameModeBase.h"
#include "VRGunGameModeBase.generated.h"

class AQteBase_New;
class ULevelSequencePlayer;

// 声明：全图 QTE 清空时的全局委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalQteClearedDelegate);
/**
 * VR 射击游戏核心模式 (GameMode)
 * 仅在服务器端运行。负责统筹玩家登录、起始联机倒计时控制、全图定序器分配，以及全局 QTE 实体管理。
 */
UCLASS()
class UE4HUNTING_API AVRGunGameModeBase : public ACMyGameModeBase
{
    GENERATED_BODY()

public:
    AVRGunGameModeBase();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#pragma region /* 玩家登录与流程控制 */
public:
    /** * 玩家连接前的拦截！用于限制最大人数
     * 如果返回的 ErrorMessage 不为空，引擎会自动拒绝该玩家加入。
     */
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

    /** 玩家成功登录并生成 Pawn 后的回调拦截，用于分配网络索引及场景标签 */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /** * 玩家中途投币加入游戏
     * @param PlayerIndex 要复活/加入的玩家索引 (通常是 1，即 2P)
     */
    UFUNCTION(BlueprintCallable, Category = "VR|GameFlow")
    void MidGameJoin(int32 PlayerIndex);

protected:
    /** 是否开启导播模式 (开启后，服务器本机将作为上帝视角，不参与游玩) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    bool bEnableDirectorMode = true;

    /** 导播实体类 (在蓝图中指定为你写的 DirectorPawn) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    TSubclassOf<class ADirectorPawn> DirectorPawnClass;

    /**缓存的导播无人机指针 (避免每次全图遍历) */
    UPROPERTY(BlueprintReadOnly, Category = "VR|Director")
    class ADirectorPawn* CachedDirector = nullptr;

    /** 独立记录真实的 VR 玩家数量 (因为导播不算真正的游玩玩家) */
    int32 ActualVRPlayerCount = 0;
    /** * 房间允许的最大游玩人数。
     * 可以在 BP_VRGunGameModeBase 蓝图面板里直接修改。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    int32 MaxPlayers = 2;

    /** 定时器回调：处理联机等待的倒计时逻辑 */
    void UpdateCountdown();

    /** 强制跳过等待，立即开始比赛并派发全局起始关卡序列 */
    void StartMatchImmediately();

    /** 开局倒计时控制句柄 */
    FTimerHandle TimerHandle_Countdown;

    /** 默认派发给全端同步播放的起始定序器 (Sequence) 资产 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Sequence")
    TMap<FString, class ULevelSequence*> LevelSequenceAssets;
#pragma endregion

#pragma region /* 实体统筹管理 */
public:
    /**
     * 全局 QTE 实例注册表。
     * 用于统筹管理当前场景中所有激活的 QTE，方便在清理关卡、播放特定全屏绝招或结算时进行快速的批量遍历操作。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|QTE")
    TArray<AQteBase_New*> AllQte;

    // 全局大喇叭：通知全服玩家或关卡蓝图
    UPROPERTY(BlueprintAssignable, Category = "Global QTE|Events")
    FOnGlobalQteClearedDelegate OnGlobalQteClearedEvent;

    // ==========================================
    // 封装的增删接口 (高内聚表现)
    // ==========================================

    UFUNCTION(BlueprintCallable, Category = "Global QTE|Functions")
    void RegisterQTE(AQteBase_New* NewQTE);

    UFUNCTION(BlueprintCallable, Category = "Global QTE|Functions")
    void UnregisterQTE(AQteBase_New* DeadQTE);

#pragma endregion

#pragma region /* 流送加载与同步亮屏发车系统 */
public:
    /** 接收客户端“已加载完毕”的汇报 */
    void PlayerFinishedLoading(class AController* Player);

    /** * 定序器专属流送触发器 (数据驱动)
     * @param SeqPlayer 当前正在播放的定序器
     * @param LevelsToLoad 需要加载的新关卡数组
     * @param LevelsToUnload 需要卸载的旧关卡数组
     * @param LoadingScreenDuration 黑屏渐变的持续时间
     * @param DelayPauseSequence 延迟多久后暂停双端的定序器
     * @param DelayLoadLevel 延迟多久后正式命令客户端开始流送加载
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Streaming")
    void TriggerTransitionFromSequencer(class ULevelSequencePlayer* SeqPlayer, const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload,float LoadingScreenDuration = 0.5f, float DelayPauseSequence = 0.0f, float DelayLoadLevel = 0.5f);

protected:
    /** 关卡切换/流送加载时，显示在电脑主屏幕上的静态图 (防止渲染目标被 GC 导致崩溃) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Director")
    class UTexture2D* SpectatorLoadingImage;

    /** 当前已经完成加载的玩家数量 */
    int32 PlayersLoadedLevel = 0;

    /** 当前是否正在进行场景过渡 (锁，防止重复触发) */
    bool bIsTransitioning = false;

    /** 缓存被暂停的定序器，以便加载完成后恢复它 */
    UPROPERTY()
    class ULevelSequencePlayer* PausedSeqPlayer = nullptr;

    /** 检查是否全员就绪，准备发车 */
    void CheckPlayersLoadingStatus();

    /** 所有人都加载完后，多等几秒再亮屏 (留给引擎编译材质和物理结算的时间) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Loading")
    float DelayBeforeStartMatch = 3.0f;

    /** 延迟亮屏的定时器句柄 */
    FTimerHandle DelayStartTimerHandle;

    /** 延迟绑定的定时器句柄 */
    FTimerHandle DelayBindTimerHandle;

    /** 真正执行收起黑屏并开始游戏的逻辑 */
    void ExecuteStartMatch();

    //延迟流送专属缓存与定时器
    FTimerHandle TimerHandle_PauseSequence;
    FTimerHandle TimerHandle_ExecuteTransition;

    TArray<FName> CachedLevelsToLoad;
    TArray<FName> CachedLevelsToUnload;

    void ExecuteDelayedPauseSequence();
    void ExecuteDelayedLevelTransition();

    FTimerHandle TimerHandle_TransitionTimeout;
    void OnTransitionTimeout();
#pragma endregion

#pragma region /* 返回大厅逻辑 */
public:
    /** * 🌟 核心功能：全房间强制返回大厅 (必须在服务器调用)
     * 可以在蓝图中直接调用（例如打完 Boss 结算后，或者有人按了退出按钮）。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|GameFlow")
    void ReturnToLobby();

protected:
    /** * 大厅的地图名称 (可在 BP_VRGunGameModeBase 蓝图里配置)
     * 注意：不要带路径，直接写名字即可，例如 "Map_Lobby"
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    FString LobbyMapName = "Map_Lobby";

    /** 延迟执行 ServerTravel 的定时器 */
    FTimerHandle TimerHandle_ReturnLobby;

    /** 真正执行无缝跳转大厅的底层函数 */
    void ExecuteReturnToLobby();
#pragma endregion

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    float MaxWaitTimeForPlayers = 30.0f;

    FTimerHandle TimerHandle_WaitForPlayers;
    bool bHasStartedMatch = false;
    void OnWaitPlayersTimeout();

#pragma region /* BGM */
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|BGM")
    TSubclassOf<class ABGMManager>BGMManager;
    UPROPERTY(BlueprintReadOnly, Category = "VR|BGM")
    class ABGMManager* CachedBGMManager = nullptr;
#pragma endregion
};