#include "VRGun/GameModeBase/VRGunGameModeBase.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
#include "VRGun/VRCharacter_Gun.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequenceActor.h"
#include "VRGun/VR_Controller/VR_PlayerController.h"
#include "LevelSequencePlayer.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "VRGun/Pawn/DirectorPawn.h"
#include "VRGun/GameInstance/VR_GameInstance.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "VRGun/Component/SequenceSyncComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "VRGun/Prop/BGMManager.h"
#pragma region /* 构造与初始化 */

AVRGunGameModeBase::AVRGunGameModeBase()
{
    bUseSeamlessTravel = false;
}

#pragma endregion

void AVRGunGameModeBase::BeginPlay()
{
    Super::BeginPlay();

    // ==========================================
    // 🌟 核心：自动生成导播机位 (仅服务器执行)
    // ==========================================
    if (bEnableDirectorMode&& DirectorPawnClass&& UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
    {
        FTransform SpawnTransform = FTransform::Identity;
        bool bFoundStart = false;

        // 寻找带有 "DirectorCamera" 标签的玩家出生点
        for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
        {
            if (It->PlayerStartTag == FName("DirectorCamera"))
            {
                SpawnTransform = It->GetActorTransform();
                bFoundStart = true;
                break;
            }
        }

        if (!bFoundStart)
        {
            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 未找到 Tag 为 'DirectorCamera' 的出生点，导播将在原点生成！"));
        }

        // 动态生成导播实体
        ADirectorPawn* SpawnedDirector = GetWorld()->SpawnActor<ADirectorPawn>(DirectorPawnClass, SpawnTransform);

        if (SpawnedDirector)
        {
            CachedDirector = SpawnedDirector;
            CachedDirector->PlayFadeTimeline(0.1f);
            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 导播无人机已自动生成并接管大屏！"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] 未配置 DirectorPawnClass，无法自动生成导播！"));
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
    }

    bHasStartedMatch = false;
    GetWorld()->GetTimerManager().SetTimer(TimerHandle_WaitForPlayers, this, &AVRGunGameModeBase::OnWaitPlayersTimeout, MaxWaitTimeForPlayers, false);

    if (HasAuthority() && BGMManager)
    {
        CachedBGMManager = GetWorld()->SpawnActor<ABGMManager>(BGMManager);
    }
}

void AVRGunGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    // ==========================================
    // 🌟 致命崩溃修复：退出游戏时必须释放定序器！
    // 否则 UE5 的 MovieScene 底层在销毁时会触发 GetPlayInEditorID 确保失败
    // ==========================================
    if (IsValid(PausedSeqPlayer))
    {
        // 强制把速度恢复，防止底层动画系统死锁
        PausedSeqPlayer->SetPlayRate(1.0f);

        // 可选：你甚至可以直接命令它停止
        // PausedSeqPlayer->Stop();

        PausedSeqPlayer = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 游戏结束，已强制释放扣押的定序器！"));
    }

    // 顺手清理所有可能残留的流送定时器
    GetWorld()->GetTimerManager().ClearTimer(TimerHandle_PauseSequence);
    GetWorld()->GetTimerManager().ClearTimer(TimerHandle_ExecuteTransition);
    GetWorld()->GetTimerManager().ClearTimer(DelayStartTimerHandle);
    GetWorld()->GetTimerManager().ClearTimer(DelayBindTimerHandle);
}

#pragma region /* 玩家登录与流程控制 */

// ==========================================
// 🌟 核心拦截机制：在玩家连接前检查人数上限
// ==========================================
void AVRGunGameModeBase::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    // 执行引擎默认的检查（比如判断是否被封禁等）
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

    // 如果前面的检查已经报错了，直接返回
    if (!ErrorMessage.IsEmpty()) return;

    // 检查当前房间真实人数是否已经达到设定的最大值
    if (GetNumPlayers() >= MaxPlayers)
    {
        // 只要给 ErrorMessage 赋值，引擎底层就会自动拒绝该客户端的网络连接！
        ErrorMessage = TEXT("Server is full. 房间人数已满，无法加入！");
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 拒绝客户端连接: 房间人数已达上限 (%d/%d)"), GetNumPlayers(), MaxPlayers);
    }
}

void AVRGunGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    AVRGunGameStateBase* GS = GetGameState<AVRGunGameStateBase>();
    if (!GS) return;

    AVRCharacter_Gun* PCCharacter = Cast<AVRCharacter_Gun>(NewPlayer->GetPawn());
    if (PCCharacter)
    {
        int32 NewIdx = ActualVRPlayerCount;
        PCCharacter->AssignedPlayerIndex = NewIdx;
        ActualVRPlayerCount++;

        // ==========================================
        // 核心：判断投币状态，决定出生标签
        // ==========================================
        UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance());
        bool bHasPaid = true; // 默认 true (如果没有接入 GameInstance)

        if (GI)
        {
            bHasPaid = (NewIdx == 0) ? GI->bPlayer0_Paid : GI->bPlayer1_Paid;
        }

        if (bHasPaid)
        {
            // 已投币：绑定正常标签 "Player_X"
            PCCharacter->MyBindingTag = FName(*FString::Printf(TEXT("Player_%d"), NewIdx));
        }
        else
        {
            // 未投币：绑定隔离标签 "Player_X_No"
            PCCharacter->MyBindingTag = FName(*FString::Printf(TEXT("Player_%d_No"), NewIdx));
        }

        // 把 Tag 同步给 SyncComp，让客户端知道自己现在是什么身份
        if (PCCharacter->SyncComp)
        {
            PCCharacter->SyncComp->BindingTag = PCCharacter->MyBindingTag;
        }

        // ==========================================
        // 寻找专属出生点 (所有人都会在这里被传送到对应的位置)
        // ==========================================
        bool bFoundStart = false;
        for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
        {
            // 引擎会自动匹配：投币的去 "Player_0"，没投币的去天空等候室 "Player_1_No"
            if (It->PlayerStartTag == PCCharacter->MyBindingTag)
            {
                PCCharacter->SetActorLocationAndRotation(It->GetActorLocation(), It->GetActorRotation());
                NewPlayer->SetControlRotation(It->GetActorRotation());
                bFoundStart = true;
                break;
            }
        }
    }
    if (PCCharacter->FindComponentByClass<UVRWeaponComponent>())
    {
        UVRWeaponComponent* WeaponComp = PCCharacter->FindComponentByClass<UVRWeaponComponent>();
        if (WeaponComp)
        {
            //装备武器
            WeaponComp->EquipInitialWeapon();
        }
       
    }
    //所有人（包括等候室的玩家）都会正常黑屏
    if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(NewPlayer))
    {
        UE_LOG(LogTemp, Warning, TEXT("bHasStartedMatch = %d"), bHasStartedMatch);
        if (!bHasStartedMatch)
        {
            VRPC->Client_ShowLoadingScreen(0.1f);
            UE_LOG(LogTemp, Warning, TEXT("正常黑屏"));
        }
    }
}

void AVRGunGameModeBase::MidGameJoin(int32 PlayerIndex)
{
    // 1. 更新 GameInstance 状态
        if (UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance()))
        {
            if (PlayerIndex == 0) GI->bPlayer0_Paid = true;
            if (PlayerIndex == 1) GI->bPlayer1_Paid = true;
        }

    // 2. 找出那个在等候室发呆的玩家
    AVR_PlayerController* TargetPC = nullptr;
    AVRCharacter_Gun* TargetChar = nullptr;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AVRCharacter_Gun* TempChar = Cast<AVRCharacter_Gun>(It->Get()->GetPawn());
        if (TempChar && TempChar->AssignedPlayerIndex == PlayerIndex)
        {
            TargetPC = Cast<AVR_PlayerController>(It->Get());
            TargetChar = TempChar;
            break;
        }
    }

    if (!TargetPC || !TargetChar) return;

    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 玩家 %d 投币成功！开始定序器折跃..."), PlayerIndex);

    // ==========================================
    // 🌟 投币瞬间的时序控制
    // ==========================================

    // 第一步：瞬间让投币玩家头显黑屏 (遮掩空间传送和定序器吸附的瞬间)
    TargetPC->Client_ShowLoadingScreen(0.5f);

    // 第二步：开启一个 0.5 秒的短延迟，等他眼前彻底黑透了，再进行标签切换
    FTimerHandle JoinTimerHandle;
    FTimerDelegate JoinDelegate = FTimerDelegate::CreateLambda([this, TargetPC, TargetChar, PlayerIndex]()
        {
            // 🌟 核心魔法：将标签从 "Player_1_No" 改回 "Player_1"
            TargetChar->MyBindingTag = FName(*FString::Printf(TEXT("Player_%d"), PlayerIndex));

            // 🌟 将其丢给定序器！
            // 只要定序器里有 "Player_1" 的 Transform 轨道，ApplyBindingAndSeek 
            // 会让底层动画系统瞬间把这个玩家吸附到当前剧情该在的准确坐标上！
            if (TargetChar->SyncComp)
            {
                // 强制解开服务器的绑定锁，允许中途换位！
                TargetChar->SyncComp->bHasLocallyBound = false;
                TargetChar->SyncComp->BindingTag = TargetChar->MyBindingTag;
                TargetChar->SyncComp->ApplyBindingAndSeek();
            }

            if (UVRWeaponComponent* WeaponComp = TargetChar->FindComponentByClass<UVRWeaponComponent>())
            {
                WeaponComp->EquipInitialWeapon(); // 投完币调用它，会自动检测地图名并换上真家伙！
            }

            // 黑屏拉起！一睁眼，已经被定序器拉到了车上，完美参战！
            TargetPC->Client_HideLoadingScreen();

            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 玩家 %d 折跃完毕，定序器已完美接管！"), PlayerIndex);
        });

    GetWorld()->GetTimerManager().SetTimer(JoinTimerHandle, JoinDelegate, 0.5f, false);
}

void AVRGunGameModeBase::UpdateCountdown()
{
    AVRGunGameStateBase* GS = GetGameState<AVRGunGameStateBase>();
    if (!GS) return;

    GS->MatchStartCountdown--;

    if (GS->MatchStartCountdown <= 0)
    {
        UE_LOG(LogTemp, Log, TEXT("倒计时结束，强制开始游戏！"));
        StartMatchImmediately();
    }
}

void AVRGunGameModeBase::StartMatchImmediately()
{
    GetWorldTimerManager().ClearTimer(TimerHandle_Countdown);

    AVRGunGameStateBase* GS = GetGameState<AVRGunGameStateBase>();
    if (!GS) return;

    if (GS->ActiveSequenceAsset == nullptr)
    {
        if (!LevelSequenceAssets.IsEmpty())
        {
            const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
            ULevelSequence* SelectLevelSequence = LevelSequenceAssets.FindRef(CurrentLevelName);
            if (SelectLevelSequence)
            {
                GS->Server_SwitchSequence(SelectLevelSequence);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Find Level Sequence Failed!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("LevelSequenceAssets is Empty!"));
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Match Started!"));
}

#pragma endregion

#pragma region /* 流送加载与同步亮屏发车系统 */

void AVRGunGameModeBase::PlayerFinishedLoading(AController* Player)
{
    PlayersLoadedLevel++;

    // 🌟 动态获取当前房间内的玩家总数
    int32 CurrentTotalPlayers = GetNumPlayers();

    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 玩家 %s 场景加载完成！当前就绪人数: %d / %d"), *Player->GetName(), PlayersLoadedLevel, CurrentTotalPlayers);

    CheckPlayersLoadingStatus();
}

void AVRGunGameModeBase::CheckPlayersLoadingStatus()
{
    if (bHasStartedMatch && !bIsTransitioning) return;
    UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance());
    int32 Expected = (GI && GI->ExpectedPlayerCount > 0) ? GI->ExpectedPlayerCount : 1;

    // 当完成加载的人数达标时
    if (GetNumPlayers() >= Expected && PlayersLoadedLevel >= Expected)
    {
        if (!bHasStartedMatch)
        {
            // ==========================================
            // 场景 A：初次开局加载完毕！
            // ==========================================
            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 【开局】全员加载完毕！等待 %f 秒后亮屏发车..."), DelayBeforeStartMatch);
            bHasStartedMatch = true; // 上锁
            GetWorld()->GetTimerManager().ClearTimer(TimerHandle_WaitForPlayers);

            // 1. 延迟亮屏并解开武器保险
            GetWorld()->GetTimerManager().SetTimer(DelayStartTimerHandle, this, &AVRGunGameModeBase::ExecuteStartMatch, DelayBeforeStartMatch, false);

            // 2. 延迟派发全局开局定序器
            float DelayStartBindMatch = DelayBeforeStartMatch - 1.0f;
            if (DelayStartBindMatch > 0.0f)
                GetWorld()->GetTimerManager().SetTimer(DelayBindTimerHandle, this, &AVRGunGameModeBase::StartMatchImmediately, DelayStartBindMatch, false);
            else
                StartMatchImmediately();
        }
        else if (bIsTransitioning)
        {
            // ==========================================
            // 场景 B：中途定序器流送加载完毕！
            // ==========================================
            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 【流送】全员流送完毕！等待 %f 秒后恢复亮屏与动画..."), DelayBeforeStartMatch);
            bIsTransitioning = false; // 🌟 提前上锁，防止延迟期间被重复触发
            GetWorld()->GetTimerManager().ClearTimer(TimerHandle_TransitionTimeout);

            // 流送完毕只需要亮屏和恢复当前定序器，不需要派发新的定序器！
            GetWorld()->GetTimerManager().SetTimer(DelayStartTimerHandle, this, &AVRGunGameModeBase::ExecuteStartMatch, DelayBeforeStartMatch, false);
        }
    }
}

void AVRGunGameModeBase::ExecuteStartMatch()
{
    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 延迟结束！全网同步亮屏，游戏正式开始！"));

    // 遍历房间内所有的玩家，强制调用 RPC 让他们收起加载图
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
        {
            VRPC->Client_HideLoadingScreen();

            //命令客户端恢复定序器播放！
            VRPC->Client_SetSequencePlayRate(1.0f);
        }
    }

    if (SpectatorLoadingImage && IsValid(CachedDirector))
    {
        if (CachedDirector->DirectorRenderTarget)
        {
            UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
            UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(CachedDirector->DirectorRenderTarget);
            UE_LOG(LogTemp, Warning, TEXT("[GameMode] 导播大屏已恢复实时画面！"));

            //触发大屏亮起
            CachedDirector->ReverseFadeTimeline(0.5f); // 0.5秒渐亮
        }
    }

    // 释放暂停的定序器演出
    if (PausedSeqPlayer)
    {
        PausedSeqPlayer->SetPlayRate(1.0f);
        PausedSeqPlayer = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 定序器恢复播放，过场演出继续！"));
    }
}

void AVRGunGameModeBase::TriggerTransitionFromSequencer(ULevelSequencePlayer* SeqPlayer, const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload, float LoadingScreenDuration, float DelayPauseSequence, float DelayLoadLevel)
{
    if (!IsValid(SeqPlayer) || bIsTransitioning) return;

    bIsTransitioning = true;
    PlayersLoadedLevel = 0;

    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 收到定序器流送请求！准备按设定延迟并发流送加载！"));

    // 🌟 防御性清理：在开启新的流送前，强制清理可能残留的幽灵定时器
    GetWorld()->GetTimerManager().ClearTimer(TimerHandle_PauseSequence);
    GetWorld()->GetTimerManager().ClearTimer(TimerHandle_ExecuteTransition);

    //开启流送专属的超时保护机制（复用 MaxWaitTimeForPlayers 的时长）
    GetWorld()->GetTimerManager().SetTimer(TimerHandle_TransitionTimeout, this, &AVRGunGameModeBase::OnTransitionTimeout, MaxWaitTimeForPlayers, false);

    // 1. 缓存核心数据
    PausedSeqPlayer = SeqPlayer;
    CachedLevelsToLoad = LevelsToLoad;
    CachedLevelsToUnload = LevelsToUnload;

    // 2. 立即执行：全网显示 Loading Screen (黑屏保护)
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        // 🌟 防御性校验：确保 Controller 和 Pawn 在此刻仍然有效
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
        {
            VRPC->Client_ShowLoadingScreen(LoadingScreenDuration);
        }
    }
    //3.导播黑屏
    if (IsValid(CachedDirector))
    {
        CachedDirector->PlayFadeTimeline(LoadingScreenDuration);
    }
    //4.显示大屏加载图
    if (bEnableDirectorMode&&SpectatorLoadingImage)
    {
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(SpectatorLoadingImage);
    }
    // 5. 延迟执行：冻结定序器演出 (包括服务器自己)
    if (DelayPauseSequence > 0.0f)
    {
        GetWorld()->GetTimerManager().SetTimer(TimerHandle_PauseSequence, this, &AVRGunGameModeBase::ExecuteDelayedPauseSequence, DelayPauseSequence, false);
    }
    else
    {
        ExecuteDelayedPauseSequence();
    }

    // 6. 延迟执行：下发名单给客户端，正式触发流送读条
    if (DelayLoadLevel > 0.0f)
    {
        GetWorld()->GetTimerManager().SetTimer(TimerHandle_ExecuteTransition, this, &AVRGunGameModeBase::ExecuteDelayedLevelTransition, DelayLoadLevel, false);
    }
    else
    {
        ExecuteDelayedLevelTransition();
    }
}

void AVRGunGameModeBase::ExecuteDelayedPauseSequence()
{
    // 🌟 致命崩溃修复：必须使用 IsValid！
    // 简单的 if(PausedSeqPlayer) 无法拦截已被虚幻 GC（垃圾回收）标记为待销毁的无效对象
    if (IsValid(PausedSeqPlayer))
    {
        PausedSeqPlayer->SetPlayRate(0.0f);
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 已执行：定序器成功冻结。"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] 延迟暂停失败：定序器在延迟期间已被销毁！"));
        PausedSeqPlayer = nullptr; // 斩断野指针
    }

    // 通知所有在线且有效的客户端冻结本地纯表现定序器
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
        {
            VRPC->Client_SetSequencePlayRate(0.0f);
        }
    }
}

void AVRGunGameModeBase::ExecuteDelayedLevelTransition()
{
    //异常中断保护：如果在这个延迟期间，服务器已经终止了流送（比如回大厅了），直接拦截
    if (!bIsTransitioning)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 延迟流送已被拦截，当前状态为非流送中。"));
        return;
    }

    // 下发缓存的关卡名单，命令客户端开始并发流送
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
        {
            VRPC->Client_ExecuteLevelTransition(CachedLevelsToLoad, CachedLevelsToUnload);
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 已执行：客户端开始流送加载旧新场景。"));
}
void AVRGunGameModeBase::OnTransitionTimeout()
{
    if (!bIsTransitioning) return;

    bIsTransitioning = false; // 强制解除流送状态
    UE_LOG(LogTemp, Error, TEXT("[GameMode] 致命警告：中途流送加载超时！强制执行亮屏并恢复定序器！"));

    // 强制执行亮屏（防止全员永久黑屏）
    ExecuteStartMatch();
}
#pragma endregion

#pragma region /* 返回大厅逻辑 */

void AVRGunGameModeBase::ReturnToLobby()
{
    // 隐患处理 1：防止连续调用（比如多个怪同时死触发了多次返回）
    if (bIsTransitioning) return;
    bIsTransitioning = true;

    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 准备返回大厅，开始全网黑屏保护..."));

    // 遍历让所有玩家黑屏，并冻结一切过场动画
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
        {
            VRPC->Client_ShowLoadingScreen(0.5f);
            VRPC->Client_SetSequencePlayRate(0.0f);
        }
    }
    if (IsValid(CachedDirector))
    {
        CachedDirector->PlayFadeTimeline(0.5f);
    }
    // 隐患处理 3：给客户端留出 1.0 秒的时间，确保摄像机的渐变黑屏完全结束，OpenXR 贴图稳定显示
    GetWorld()->GetTimerManager().SetTimer(TimerHandle_ReturnLobby, this, &AVRGunGameModeBase::ExecuteReturnToLobby, 1.0f, false);
}

void AVRGunGameModeBase::ExecuteReturnToLobby()
{
    //返回大厅前：同样在备忘录里写下真实人数
    if (UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance()))
    {
        GI->ExpectedPlayerCount = GetNumPlayers();
    }
    // 如果没有配置大厅名字，给个报错警告
    if (LobbyMapName.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] 无法返回大厅：LobbyMapName 为空！"));
        bIsTransitioning = false;
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[GameMode] 正在执行 ServerTravel 返回大厅: %s"), *LobbyMapName);

    if (bEnableDirectorMode&&SpectatorLoadingImage)
    {
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(SpectatorLoadingImage);
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 导播大屏已切换为静态 Loading 图！"));
    }

    // 隐患处理 4：清理当前 GameMode 上的所有遗留定时器，防止内存泄漏或野指针崩溃
    GetWorld()->GetTimerManager().ClearAllTimersForObject(this);

    // 最终一击：带着所有人（含各种客户端）直接飞回大厅！
    GetWorld()->ServerTravel(LobbyMapName);
}

#pragma endregion

#pragma region /* QTE增删改查 */
void AVRGunGameModeBase::RegisterQTE(AQteBase_New* NewQTE)
{
    if (NewQTE)
    {
        // AddUnique 防止意外重复添加
        AllQte.AddUnique(NewQTE);
    }
}

void AVRGunGameModeBase::UnregisterQTE(AQteBase_New* DeadQTE)
{
    if (DeadQTE)
    {
        AllQte.Remove(DeadQTE);

        // 核心判定：花名册被清空了！
        if (AllQte.Num() == 0)
        {
            // 触发全服广播！
            if (OnGlobalQteClearedEvent.IsBound())
            {
                OnGlobalQteClearedEvent.Broadcast();
            }
        }
    }
}
#pragma endregion

void AVRGunGameModeBase::OnWaitPlayersTimeout()
{
    if (bHasStartedMatch) return;
    bHasStartedMatch = true;

    UE_LOG(LogTemp, Error, TEXT("[GameMode] 致命警告：等待玩家连接或加载超时！以当前已就绪人数强制开始游戏！"));

    // 强制执行你原本的亮屏发车逻辑
    ExecuteStartMatch();
    StartMatchImmediately();
}