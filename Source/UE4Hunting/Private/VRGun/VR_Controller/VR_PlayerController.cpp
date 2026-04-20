#include "VRGun/VR_Controller/VR_PlayerController.h"
#include "XRLoadingScreenFunctionLibrary.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "VRGun/GameModeBase/VRGunGameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "VRGun/QTE/QteBase_New.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "VRGun/GameModeBase/VRLobbyGameMode.h"
void AVR_PlayerController::BeginPlay()
{
    Super::BeginPlay();

}

void AVR_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}
void AVR_PlayerController::StartListenServer(FString MapName)
{
    UWorld* World = GetWorld();
    if (World)
    {
        FString TravelURL = MapName + TEXT("?listen");
        World->ServerTravel(TravelURL, true);
    }
}

void AVR_PlayerController::JoinServer(FString IPAddress)
{
    this->ClientTravel(IPAddress, ETravelType::TRAVEL_Absolute);
}

#pragma region /* VR 无缝加载与流送握手 */

void AVR_PlayerController::Client_ShowLoadingScreen_Implementation(float Duration)
{
    if (PlayerCameraManager)
    {
        PlayerCameraManager->StartCameraFade(0.0f, 1.0f, Duration, FLinearColor::Black, false, true);
    }
    // 立刻给武器上保险
    if (APawn* MyPawn = GetPawn())
    {
        if (UVRWeaponComponent* WeaponComp = MyPawn->FindComponentByClass<UVRWeaponComponent>())
        {
            WeaponComp->SetCanFire(false);
        }
    }
}

void AVR_PlayerController::Client_HideLoadingScreen_Implementation()
{

    if (PlayerCameraManager)
    {
        PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.5f, FLinearColor::Black, false, true);
    }
    // 解开武器保险
    if (APawn* MyPawn = GetPawn())
    {
        if (UVRWeaponComponent* WeaponComp = MyPawn->FindComponentByClass<UVRWeaponComponent>())
        {
            WeaponComp->SetCanFire(true);
        }
    }
}

bool AVR_PlayerController::Server_NotifyLevelLoaded_Validate()
{
    return true;
}

void AVR_PlayerController::Server_NotifyLevelLoaded_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("[Server] 收到客户端流送加载完毕通知: %s"), *GetName());

    AGameModeBase* GM = GetWorld()->GetAuthGameMode();
    if (AVRGunGameModeBase* GunGM = Cast<AVRGunGameModeBase>(GM))
    {
        GunGM->PlayerFinishedLoading(this);
    }
    else if (AVRLobbyGameMode* LobbyGM = Cast<AVRLobbyGameMode>(GM))
    {
        LobbyGM->PlayerFinishedLoading(this);
    }
}

void AVR_PlayerController::Client_ExecuteLevelTransition_Implementation(const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload)
{
    // 1. 缓存服务器下发的流送名单
    PendingLevelsToLoad = LevelsToLoad;
    PendingLevelsToUnload = LevelsToUnload;

    // 2. 开启一个 0.5 秒的短延迟定时器。
    // 因为 Client_ShowLoadingScreen 里的黑屏渐变需要 0.2 秒，
    // 我们留出 0.5 秒的绝对安全时间，确保玩家眼前彻底黑透且 XR Logo 已稳定显示，再开始榨干硬盘性能！
    FTimerHandle DelayLoadTimer;
    GetWorld()->GetTimerManager().SetTimer(DelayLoadTimer, this, &AVR_PlayerController::DoExecuteLevelTransition, 1.0f, false);
}

void AVR_PlayerController::OnSingleStreamLevelLoaded()
{
    CurrentLoadedCount++;
    UE_LOG(LogTemp, Warning, TEXT("[Client] 单个流送关卡加载完成！本地进度: %d / %d"), CurrentLoadedCount, TargetLoadCount);

    if (CurrentLoadedCount >= TargetLoadCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] 所有指定关卡加载完毕！向服务器汇报就绪。"));
        Server_NotifyLevelLoaded();
    }
}

void AVR_PlayerController::Client_SetSequencePlayRate_Implementation(float PlayRate)
{
    // 获取全局状态机，找到本地专属的定序器并修改速率
    if (AVRGunGameStateBase* GS = GetWorld()->GetGameState<AVRGunGameStateBase>())
    {
        if (GS->LocalSequenceActor)
        {
            if (ULevelSequencePlayer* SeqPlayer = GS->LocalSequenceActor->GetSequencePlayer())
            {
                SeqPlayer->SetPlayRate(PlayRate);
            }
        }
    }
}

void AVR_PlayerController::DoExecuteLevelTransition()
{
    //统一定义一个静态 UUID，给所有的异步流送操作使用
    static int32 ActionUUID = 0;

    // 1. 瞬间发起所有旧关卡的卸载请求
    for (const FName& UnloadLevel : PendingLevelsToUnload)
    {
        FLatentActionInfo LatentInfo;
        // 给每一个卸载任务分配独一无二的 UUID，防止被引擎底层合并或丢弃
        LatentInfo.UUID = ActionUUID++;

        UGameplayStatics::UnloadStreamLevel(this, UnloadLevel, LatentInfo, false);
    }

    // 2. 初始化并发加载计数器
    TargetLoadCount = PendingLevelsToLoad.Num();
    CurrentLoadedCount = 0;

    // 如果不需要加载新关卡，直接向服务器交差
    if (TargetLoadCount == 0)
    {
        Server_NotifyLevelLoaded();
        return;
    }

    // 3. 瞬间发起所有新关卡的并发加载请求
    for (const FName& LoadLevel : PendingLevelsToLoad)
    {
        FLatentActionInfo LatentInfo;
        LatentInfo.CallbackTarget = this;
        LatentInfo.ExecutionFunction = FName("OnSingleStreamLevelLoaded");
        LatentInfo.Linkage = 0;

        // 加载任务继续使用递增的 UUID
        LatentInfo.UUID = ActionUUID++;

        UGameplayStatics::LoadStreamLevel(this, LoadLevel, true, false, LatentInfo);
    }
}
#pragma endregion

#pragma region /* 同步QTE生命周期 */
void AVR_PlayerController::Server_ReportQTEReady_Implementation(AQteBase_New* ReadyQTE)
{
    // 服务器收到了客户端的汇报！
    if (ReadyQTE && HasAuthority())
    {
        // 🌟 核心：服务器终于可以在这里统一设置寿命了！
        ReadyQTE->SetLifeSpan(ReadyQTE->InitData.Duration);

        // 如果需要，服务器可以在这里发一个多播，通知所有人：“时间统一开始跑啦！”
        ReadyQTE->Multicast_StartQTETimer();
    }
}
#pragma endregion
