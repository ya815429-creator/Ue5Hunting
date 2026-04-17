#include "VRGun/GameInstance/VR_GameInstance.h"

#include "HAL/RunnableThread.h"
#include "Kismet/GameplayStatics.h"
#include "MyBlueprintFunctionLibrary.h"
#include "VRGun/GameModeBase/VRLobbyGameMode.h"
#include "VRGun/GameModeBase/VRGunGameModeBase.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
void UVR_GameInstance::Init()
{
    Super::Init();

    //核心：如果是展会免费模式，初始化时直接拉满投币状态
    if (bExhibitionFreeMode)
    {
        bPlayer0_Paid = true;
        bPlayer1_Paid = true;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 已开启展会免费模式，全自动授权游玩！"));
    }

    // 1. 从 JSON 读取身份
    FString ret;
    if (UMyBlueprintFunctionLibrary::ReadArray(TEXT("NetworkSettings"), TEXT("bIsServer"), ret))
    {
        // 直接根据字符串是否等于 "NO" 来赋值，避免冗余的 bool 临时变量
        bIsHostServer = (ret != TEXT("NO"));
    }

    // 2. 初始化防连发锁
    bIsConnectingToServer = false;

    // 3. 实例化 Worker
    TWeakObjectPtr<UVR_GameInstance> WeakThis(this);
    DiscoveryWorker = new FServerDiscoveryWorker(bIsHostServer, [WeakThis](FString IP)
        {
            AsyncTask(ENamedThreads::GameThread, [WeakThis, IP]()
                {
                    if (UVR_GameInstance* StrongThis = WeakThis.Get())
                    {
                        StrongThis->ConnectToFoundServer(IP);
                    }
                });
        });

    WorkerThread = FRunnableThread::Create(DiscoveryWorker, TEXT("DiscoveryThread"));
}

void UVR_GameInstance::ConnectToFoundServer(FString IPAddress)
{
    // 🌟 防连发拦截：如果已经正在连接了，直接忽略后续的 UDP 广播
    if (bIsConnectingToServer) return;

    APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    if (PC)
    {
        bIsConnectingToServer = true; // 上锁！

        UE_LOG(LogTemp, Warning, TEXT("[Network] 成功捕获动态服务端 IP: %s，开始发起连接！"), *IPAddress);

        // 执行加入
        PC->ClientTravel(IPAddress, ETravelType::TRAVEL_Absolute);
    }
}

void UVR_GameInstance::Shutdown()
{
    // 停止并清理线程资源，防止退出游戏时崩溃
    if (DiscoveryWorker) DiscoveryWorker->Stop();

    if (WorkerThread)
    {
        WorkerThread->Kill(true);
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    if (DiscoveryWorker)
    {
        delete DiscoveryWorker;
        DiscoveryWorker = nullptr;
    }

    Super::Shutdown();
}

void UVR_GameInstance::BeginDestroy()
{
    // 强制保障：在 GC 彻底回收内存前，无条件停止并销毁后台线程
    if (DiscoveryWorker) DiscoveryWorker->Stop();

    if (WorkerThread)
    {
        WorkerThread->Kill(true); // 阻塞直到线程安全退出
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    if (DiscoveryWorker)
    {
        delete DiscoveryWorker;
        DiscoveryWorker = nullptr;
    }

    Super::BeginDestroy();
}

void UVR_GameInstance::InsertCoin(int32 PlayerIndex)
{
    // 免费模式不扣币不记数，直接拦截
    if (bExhibitionFreeMode) return;

    bool bJustActivated = false;

    if (PlayerIndex == 0)
    {
        Player0_RawCoins++;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] P0 投币！当前溢出池: %d/%d"), Player0_RawCoins, CoinsPerCredit);

        // 如果玩家正在小黑屋/未激活状态，且币够了，立刻扣除并激活！
        if (!bPlayer0_Paid && Player0_RawCoins >= CoinsPerCredit)
        {
            Player0_RawCoins -= CoinsPerCredit;
            bPlayer0_Paid = true;
            bJustActivated = true;
            UE_LOG(LogTemp, Warning, TEXT("[GameInstance] P0 扣币激活！剩余溢出池: %d"), Player0_RawCoins);
        }
    }
    else if (PlayerIndex == 1)
    {
        Player1_RawCoins++;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] P1 投币！当前溢出池: %d/%d"), Player1_RawCoins, CoinsPerCredit);

        if (!bPlayer1_Paid && Player1_RawCoins >= CoinsPerCredit)
        {
            Player1_RawCoins -= CoinsPerCredit;
            bPlayer1_Paid = true;
            bJustActivated = true;
            UE_LOG(LogTemp, Warning, TEXT("[GameInstance] P1 扣币激活！剩余溢出池: %d"), Player1_RawCoins);
        }
    }

    // 🌟 1. 立刻同步给当前关卡的 GameState，全网 3D UI 瞬间跳字！
    SyncCoinDataToGameState();

    // 🌟 2. 如果这次投币刚好激活了玩家，主动通知 GameMode 发枪或发车！
    if (bJustActivated)
    {
        AGameModeBase* CurrentGM = UGameplayStatics::GetGameMode(this);
        if (AVRLobbyGameMode* LobbyGM = Cast<AVRLobbyGameMode>(CurrentGM))
        {
            // 如果在大厅，通知大厅没收彩带枪发真枪
            LobbyGM->OnPlayerInsertedCoin(PlayerIndex);
        }
        else if (AVRGunGameModeBase* GunGM = Cast<AVRGunGameModeBase>(CurrentGM))
        {
            // 如果在战斗关卡，执行中途加入黑屏折跃
            GunGM->MidGameJoin(PlayerIndex);
        }
    }
}

void UVR_GameInstance::ResetCoinStatus()
{
    if (bExhibitionFreeMode) return;

    // 🌟 自动续关判定 (单局打完时触发)

    // Player 0
    if (Player0_RawCoins >= CoinsPerCredit)
    {
        Player0_RawCoins -= CoinsPerCredit;
        bPlayer0_Paid = true;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 游戏结束，P0 溢出币充足(扣除后剩 %d)，已自动续玩！"), Player0_RawCoins);
    }
    else
    {
        bPlayer0_Paid = false;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 游戏结束，P0 币不足，退回未激活状态。"));
    }

    // Player 1
    if (Player1_RawCoins >= CoinsPerCredit)
    {
        Player1_RawCoins -= CoinsPerCredit;
        bPlayer1_Paid = true;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 游戏结束，P1 溢出币充足(扣除后剩 %d)，已自动续玩！"), Player1_RawCoins);
    }
    else
    {
        bPlayer1_Paid = false;
        UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 游戏结束，P1 币不足，退回未激活状态。"));
    }

    // 更新 UI
    SyncCoinDataToGameState();
}

void UVR_GameInstance::SyncCoinDataToGameState()
{
    // 只有服务器的 GameInstance 才能获取到有效的 GameState 并修改同步变量
    if (AVRGunGameStateBase* VRGS = Cast<AVRGunGameStateBase>(UGameplayStatics::GetGameState(this)))
    {
        // 算出散币 (取余) 和 信用点 (整除)，推给 GameState
        VRGS->UpdateCoinData(
            bExhibitionFreeMode, CoinsPerCredit,
            (Player0_RawCoins % CoinsPerCredit), (Player0_RawCoins / CoinsPerCredit), bPlayer0_Paid,
            (Player1_RawCoins % CoinsPerCredit), (Player1_RawCoins / CoinsPerCredit), bPlayer1_Paid
        );
    }
}

void UVR_GameInstance::ExecuteStartupFlow()
{
    // 如果我是服务端，我需要主动把当前的启动地图转换成“监听服务器(Listen Server)”状态！
    if (bIsHostServer)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Network] 本机为服务端，正在开启局域网监听..."));

        // 假设你要开的房间是大厅，加上 ?listen 参数
        FString ListenURL = TEXT("/Game/GameVRGun/Map/LobbyMap?listen");
        GetWorld()->ServerTravel(ListenURL, true);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Network] 本机为客户端，正在后台不断寻找服务端..."));
        // 客户端什么都不用做，就待在原地看 Loading 图，等 UDP 找到 IP 触发回调
    }
}