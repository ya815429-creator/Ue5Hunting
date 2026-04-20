// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/GameModeBase/VRLobbyGameMode.h"
#include "VRGun/VR_Controller/VR_PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "VRGun/VRCharacter_Gun.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "VRGun/Pawn/DirectorPawn.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "VRGun/GameInstance/VR_GameInstance.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "VRGun/Prop/BGMManager.h"
AVRLobbyGameMode::AVRLobbyGameMode()
{
    // 【核心机制】：强制使用有缝跳转 (Non-Seamless Travel)。
    // 这会在跳转时彻底销毁旧地图的所有 Actor 和 Widget，是防止崩溃和内存泄漏的最稳妥方式。
    bUseSeamlessTravel = false;
}

// ==========================================
// 大厅人数拦截机制
// ==========================================
void AVRLobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) return;

    // 检查大厅当前人数是否已满
    if (GetNumPlayers() >= MaxPlayers)
    {
        ErrorMessage = TEXT("Server is full. 大厅人数已满，无法加入！");
        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 拒绝客户端连接: 大厅人数已达上限 (%d/%d)"), GetNumPlayers(), MaxPlayers);
    }
}

// ==========================================
// 大厅站位精准分配
// ==========================================
void AVRLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    AVRCharacter_Gun* PCCharacter = Cast<AVRCharacter_Gun>(NewPlayer->GetPawn());
    if (PCCharacter)
    {
        // 1. 获取当前已登录的人数作为玩家索引 
         // 使用独立计数器！
         // 这样即使有了导播，第一个进来的 VR 玩家依然是 Player_0
        int32 NewIdx = ActualVRPlayerCount;
        PCCharacter->AssignedPlayerIndex = NewIdx;


        // 2. 自动拼接 Tag (例如: "Player_0", "Player_1")
        PCCharacter->MyBindingTag = FName(*FString::Printf(TEXT("Player_%d"), NewIdx));
        UE_LOG(LogTemp, Log, TEXT("玩家已登录: Index %d, Tag %s"), NewIdx, *PCCharacter->MyBindingTag.ToString());

        // 真实玩家数 + 1
        ActualVRPlayerCount++;
        // 3. 寻找大厅专属出生点并强制传送
        bool bFoundStart = false;

        for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
        {
            APlayerStart* StartActor = *It;

            // 匹配出生点的标签和玩家的标签
            if (StartActor->PlayerStartTag == PCCharacter->MyBindingTag)
            {
                // 将玩家角色模型传送到该位置
                PCCharacter->SetActorLocationAndRotation(StartActor->GetActorLocation(), StartActor->GetActorRotation());
                // 同步 VR 控制器的正前方朝向
                NewPlayer->SetControlRotation(StartActor->GetActorRotation());

                UE_LOG(LogTemp, Warning, TEXT("[Lobby] 玩家 %s 成功锁定大厅出生点: %s"), *PCCharacter->MyBindingTag.ToString(), *StartActor->GetName());
                bFoundStart = true;
                break;
            }
        }

        if (!bFoundStart)
        {
            UE_LOG(LogTemp, Error, TEXT("[Lobby] 找不到标签为 %s 的 PlayerStart！玩家将在大厅原地生成。"), *PCCharacter->MyBindingTag.ToString());
        }

        if (UVRWeaponComponent* WeaponComp = PCCharacter->FindComponentByClass<UVRWeaponComponent>())
        {
            // 系统会自动判断：没投币发彩带枪，投了币发地图专属枪
            WeaponComp->EquipInitialWeapon();
        }
    }
    //让刚进来的玩家保持黑屏
    if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(NewPlayer))
    {
        if (!bHasStartedLobby)
        {
            VRPC->Client_ShowLoadingScreen(0.1f);
        }
    }
}

void AVRLobbyGameMode::BeginPlay()
{
    Super::BeginPlay();

    bHasStartedLobby = false;
    PlayersLoadedLevel = 0;
    GetWorld()->GetTimerManager().SetTimer(TimerHandle_WaitForPlayers, this, &AVRLobbyGameMode::OnWaitPlayersTimeout, MaxWaitTimeForPlayers, false);

    if (bEnableDirectorMode)
    {
        if (DirectorPawnClass&& UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
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
        }
    }
    else
    {
        // 如果关闭了导播，确保重置电脑屏幕的输出模式为头显画面！
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] 导播模式已禁用，电脑大屏将显示默认头显画面。"));
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
    }


    if (HasAuthority()&& BGMManager)
    {
        CachedBGMManager = GetWorld()->SpawnActor<ABGMManager>(BGMManager);
    }
}

void AVRLobbyGameMode::SubmitVote(FName LevelName, AController* Voter)
{
    // 如果已经发车了，或者关卡名为空，直接忽略
    if (bIsTraveling || LevelName.IsNone()) return;

    // 【防连发机制】：距离上次有效投票不到 1 秒，判定为无效扫射
    float CurrentTime = GetWorld()->GetTimeSeconds();
    if (CurrentTime - LastVoteTime < VoteCooldown) return;

    // 更新最后一次有效射击的时间
    LastVoteTime = CurrentTime;

    // 核心修改：判断是“确认确认”还是“改变主意”
    if (CurrentVotedLevel == LevelName)
    {
        // 连续打中了同一个靶子，票数累加！
        CurrentVotes++;
        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 连续确认！目标关卡: %s, 当前总票数: %d/2"), *LevelName.ToString(), CurrentVotes);
    }
    else
    {
        // 第一次打这个靶子，或者中途改变主意打了别的靶子！
        // 重置追踪的关卡为新目标，并且票数直接重置为 1 票。
        CurrentVotedLevel = LevelName;
        CurrentVotes = 1;
        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 锁定新目标！目标关卡: %s, 票数重置为: 1/2"), *LevelName.ToString());
    }

    // 当总票数达到 2 票时，触发发车流程！
    if (CurrentVotes >= 2)
    {
        bIsTraveling = true;
        FinalLevelToLoad = LevelName;

        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 票数达成！准备跳转至关卡: %s"), *LevelName.ToString());

        // 1. 遍历让所有玩家黑屏
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
            {
                VRPC->Client_ShowLoadingScreen(0.5f);
            }
        }
        if (IsValid(CachedDirector))
        {
            CachedDirector->PlayFadeTimeline(0.5f);
        }
        /* 2. 延迟执行跳转*/
        FTimerHandle TravelTimerHandle;
        GetWorld()->GetTimerManager().SetTimer(TravelTimerHandle, this, &AVRLobbyGameMode::ExecuteServerTravel, 1.0f, false);
    }
}

void AVRLobbyGameMode::OnPlayerInsertedCoin(int32 PlayerIndex)
{
    // 1. 更新 GameInstance 中的投币状态，记录该玩家已付款
    if (UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance()))
    {
        if (PlayerIndex == 0) GI->bPlayer0_Paid = true;
        if (PlayerIndex == 1) GI->bPlayer1_Paid = true;
    }

    // 2. 遍历找出那个刚投币的玩家
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AVRCharacter_Gun* TargetChar = Cast<AVRCharacter_Gun>(It->Get()->GetPawn());
        if (TargetChar && TargetChar->AssignedPlayerIndex == PlayerIndex)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Lobby] 玩家 %d 在大厅投币成功！原地发放选关真枪..."), PlayerIndex);

            // 3. 核心：重新调用智能发枪逻辑！
            // 此时 GameInstance 里的状态已经是 True 了，系统会自动把彩带枪顶替掉，换成 MapDefaultWeaponMap 里配置的大厅真枪
            if (UVRWeaponComponent* WeaponComp = TargetChar->FindComponentByClass<UVRWeaponComponent>())
            {
                WeaponComp->EquipInitialWeapon();
            }
            break;
        }
    }
}

void AVRLobbyGameMode::ExecuteServerTravel()
{
    if (UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance()))
    {
        GI->ExpectedPlayerCount = GetNumPlayers();
    }
    // 执行底层的关卡跳转，"?listen" 确保服务器在跳转后依然保持局域网主机状态
    FString URL = FinalLevelToLoad.ToString() + TEXT("?listen");
    if (bEnableDirectorMode&&SpectatorLoadingImage)
    {
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(SpectatorLoadingImage);
        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 准备发车！导播大屏已切换为静态 Loading 图！"));
    }
    GetWorld()->ServerTravel(URL, false);
}

void AVRLobbyGameMode::PlayerFinishedLoading(AController* Player)
{
    PlayersLoadedLevel++;
    CheckAndStartLobby();
}

void AVRLobbyGameMode::CheckAndStartLobby()
{
    if (bHasStartedLobby) return;

    UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance());
    int32 Expected = (GI && GI->ExpectedPlayerCount > 0) ? GI->ExpectedPlayerCount : 1;

    // 🌟 双重屏障：连接人齐了 && 加载图读完了
    if (GetNumPlayers() >= Expected && PlayersLoadedLevel >= Expected)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Lobby] 大厅全员就绪！取消超时，全体亮屏。"));
        bHasStartedLobby = true;
        GetWorld()->GetTimerManager().ClearTimer(TimerHandle_WaitForPlayers);
		GetWorld()->GetTimerManager().SetTimer(DelayStartTimerHandle, this, &AVRLobbyGameMode::LightUpAllPlayers, DelayBeforeStartMatch, false);
    }
}

void AVRLobbyGameMode::OnWaitPlayersTimeout()
{
    if (bHasStartedLobby) return;
    bHasStartedLobby = true;
    UE_LOG(LogTemp, Error, TEXT("[Lobby] 大厅等待超时！强制亮屏！"));
    LightUpAllPlayers();
}

void AVRLobbyGameMode::LightUpAllPlayers()
{
    // 大厅统一亮屏（此时会自动调用客户端控制器解开武器保险！）
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVR_PlayerController* VRPC = Cast<AVR_PlayerController>(It->Get()))
            VRPC->Client_HideLoadingScreen();
    }
    if (CachedDirector) CachedDirector->ReverseFadeTimeline(0.5f);
    if (CachedBGMManager)
    {
        CachedBGMManager->SwitchBGM(TEXT("Lobby"));
    }
}