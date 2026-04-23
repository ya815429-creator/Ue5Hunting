// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VRLobbyGameMode.generated.h"

/**
 * 
 */
UCLASS()
class UE4HUNTING_API AVRLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
    AVRLobbyGameMode();
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void BeginPlay() override;
    /** 接收来自靶子的投票申请
     * @param LevelName 玩家选择的关卡名
     * @param Voter 开枪的玩家控制器 (用于去重或记录)
     */
    UFUNCTION(BlueprintCallable)
    void SubmitVote(FName LevelName, class AController* Voter);

    /*当玩家投币时*/
    void OnPlayerInsertedCoin(int32 PlayerIndex);
protected:

protected:
    /** 是否开启导播模式 (开启后，服务器本机将作为上帝视角，不参与游玩) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    bool bEnableDirectorMode = true;

    /** 导播实体类 (在蓝图中指定为你写的 DirectorPawn) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    TSubclassOf<class ADirectorPawn> DirectorPawnClass;

    /**缓存的导播指针*/
    UPROPERTY(BlueprintReadOnly, Category = "VR|Director")
    class ADirectorPawn* CachedDirector = nullptr;

    /** 大厅发车前往战斗关卡时，显示在主屏幕上的静态图 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Director")
    class UTexture2D* SpectatorLoadingImage;

    /** 独立记录真实的 VR 玩家数量 (因为导播不算真正的游玩玩家) */
    int32 ActualVRPlayerCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    int32 MaxPlayers = 2;

    /** 记录当前关卡获得了几票 */
    int32 CurrentVotes = 0;

    /** 记录上一次有效投票的时间 (防止加特林一瞬间打出 2 票) */
    float LastVoteTime = 0.0f;

    /** 防连发冷却时间 (秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    float VoteCooldown = 1.0f;

    //当前正在被投票的关卡名
    FName CurrentVotedLevel;

    /** 准备跳转的最终关卡 */
    FName FinalLevelToLoad;

    /** 状态锁：是否已经在执行跳转发车流程 */
    bool bIsTraveling = false;

    /** 执行最终的 ServerTravel */
    void ExecuteServerTravel();

    /** 所有人都加载完后，多等几秒再亮屏 (留给引擎编译材质和物理结算的时间) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Loading")
    float DelayBeforeStartMatch = 3.0f;

    /** 延迟亮屏的定时器句柄 */
    FTimerHandle DelayStartTimerHandle;
public:
    void PlayerFinishedLoading(class AController* Player);
protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|GameSettings")
    float MaxWaitTimeForPlayers = 45.0f;

    FTimerHandle TimerHandle_WaitForPlayers;
    bool bHasStartedLobby = false;
    int32 PlayersLoadedLevel = 0;

    void CheckAndStartLobby();
    void OnWaitPlayersTimeout();
    void LightUpAllPlayers();

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|BGM")
    TSubclassOf<class ABGMManager>BGMManager;
    UPROPERTY(BlueprintReadOnly, Category = "VR|BGM")
    class ABGMManager* CachedBGMManager = nullptr;
};
