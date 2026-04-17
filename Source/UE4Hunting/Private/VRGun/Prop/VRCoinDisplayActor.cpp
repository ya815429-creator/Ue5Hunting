// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Prop/VRCoinDisplayActor.h"
#include "Components/WidgetComponent.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
#include "VRGun/Widget/VRCoinWidget.h"
#include "Kismet/GameplayStatics.h"
// Sets default values
AVRCoinDisplayActor::AVRCoinDisplayActor()
{
    // 允许 Tick 轮询监听
    PrimaryActorTick.bCanEverTick = true;

    // 创建并设置 3D UI 组件
    WidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComponent"));
    RootComponent = WidgetComponent;

    // 设置默认的 3D UI 渲染模式
    WidgetComponent->SetWidgetSpace(EWidgetSpace::World);
    WidgetComponent->SetDrawSize(FVector2D(800.0f, 500.0f));

    // 初始化缓存数据，设为异常值保证第一次必定刷新
    LastCoins = -1;
    LastCredits = -1;
    bLastPaid = false;
    bLastFreeMode = false;

}

// Called when the game starts or when spawned
void AVRCoinDisplayActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AVRCoinDisplayActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. 获取全网同步的 GameState
    AVRGunGameStateBase* GS = Cast<AVRGunGameStateBase>(UGameplayStatics::GetGameState(this));
    if (!GS) return;

    // 2. 获取屏幕上挂载的 UI 实例
    UVRCoinWidget* CoinUI = Cast<UVRCoinWidget>(WidgetComponent->GetUserWidgetObject());
    if (!CoinUI) return;

    // 3. 根据这台机器绑定的玩家索引 (0 或 1)，提取对应的数据
    int32 CurCoins = (TargetPlayerIndex == 0) ? GS->Rep_P0_Coins : GS->Rep_P1_Coins;
    int32 CurCredits = (TargetPlayerIndex == 0) ? GS->Rep_P0_Credits : GS->Rep_P1_Credits;
    bool bPaid = (TargetPlayerIndex == 0) ? GS->Rep_P0_IsPaid : GS->Rep_P1_IsPaid;
    bool bFreeMode = GS->Rep_bIsFreeMode;
    int32 ReqCoins = GS->GlobalCoinsPerCredit;

    // 4. 性能优化校验：如果数据和上一帧一模一样，什么都不做
    if (CurCoins != LastCoins || CurCredits != LastCredits || bPaid != bLastPaid || bFreeMode != bLastFreeMode)
    {
        // 数据变了！触发 UI 更新节点
        CoinUI->OnCoinDataUpdated(TargetPlayerIndex,CurCoins, ReqCoins, CurCredits, bPaid, bFreeMode);

        // 记录新的状态
        LastCoins = CurCoins;
        LastCredits = CurCredits;
        bLastPaid = bPaid;
        bLastFreeMode = bFreeMode;
    }

}

