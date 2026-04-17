// Fill out your copyright notice in the Description page of Project Settings.

#include "VRGun/Prop/EnemySpawnActor.h"
#include "VRGun/Enemy/EnemyBase_New.h"
#include "Components/ArrowComponent.h"
#include "TimerManager.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h" 
#include "VRGun/Component/SequenceSyncComponent.h"
#include "Kismet/GameplayStatics.h"

#pragma region /* 生命周期与初始化 */

/**
 * 构造函数：初始化怪物生成器。
 * 考虑到性能和网络带宽，生成器仅在服务器运行并负责逻辑调度，不需要下发至客户端。
 */
AEnemySpawnActor::AEnemySpawnActor()
{
    PrimaryActorTick.bCanEverTick = false; // 生成器本身不需要 Tick，依靠 Timer 驱动

    // 【网络优化】：生成器纯服务器运作，严禁网络同步
    bReplicates = false;

    // 移除不必要的渲染组件，仅保留纯空场景节点作为占位 Root
    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    RootComponent = SceneRoot;
}

void AEnemySpawnActor::BeginPlay()
{
    Super::BeginPlay();
}

#pragma endregion


#pragma region /* 生成逻辑与回调调度 */

/**
 * 启动生成序列主入口
 * 接收来自蓝图或关卡序列的生成数据（如数量、间隔、各怪物的专属 Tag 与血量）。
 */
void AEnemySpawnActor::MainSpawn(FSpawnEnemyStruct InSpawnData)
{
    // 安全屏障：必须在服务器端执行
    if (!HasAuthority()) return;

    // 缓存数据，供后续定时器或循环使用
    ActiveSpawnData = InSpawnData;

    // 严格校验数据数组长度，防止越界崩溃
    if (ActiveSpawnData.SpawnNumber > 0 &&
        ActiveSpawnData.SequenceTags.Num() >= ActiveSpawnData.SpawnNumber &&
        ActiveSpawnData.MaxHp.Num() >= ActiveSpawnData.SpawnNumber)
    {
        CurrentSpawnNumber = 0;

        // 【极速模式】：如果延迟时间为 0，则在当前帧瞬间循环生成全部怪物
        if (ActiveSpawnData.SpawnDelayTime <= 0.0f)
        {
            int32 TotalSpawns = ActiveSpawnData.SpawnNumber;
            for (int32 i = 0; i < TotalSpawns; ++i)
            {
                SpawnCallBack();
            }
        }
        else
        {
            // 【延迟模式】：根据配置的间隔时间启动定时器分批生成
            GetWorldTimerManager().SetTimer(
                TimerHandle_Spawn,
                this,
                &AEnemySpawnActor::SpawnCallBack,
                ActiveSpawnData.SpawnDelayTime,
                true,
                0.0f
            );
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Error: 怪物生成数据校验失败！参数数组长度不匹配。发生于生成器: %s"), *GetName());
    }
}

/**
 * 单体怪物生成与属性注入逻辑 (定时器回调/循环调用)
 */
void AEnemySpawnActor::SpawnCallBack()
{
    if (!HasAuthority() || !EnemyClass) return;

    // 1. 根据当前计数器索引，提取该只怪物的专属数据
    FName TempSequenceTag = ActiveSpawnData.SequenceTags[CurrentSpawnNumber];
    float TempMaxHP = ActiveSpawnData.MaxHp[CurrentSpawnNumber];

    // 防呆设计：如果策划忘记配置奖励数组或长度不足，默认提供 EWeaponRewardType::None
    EWeaponRewardType TempReward = EWeaponRewardType::None;
    if (ActiveSpawnData.RewardTypes.IsValidIndex(CurrentSpawnNumber))
    {
        TempReward = ActiveSpawnData.RewardTypes[CurrentSpawnNumber];
    }

    // 2. 开启延迟生成 (SpawnActorDeferred)
    // 此阶段怪物仅分配了内存，尚未触发 BeginPlay，也不会向客户端广播生成 RPC，便于我们在底层注入参数
    AEnemyBase_New* SpawnedEnemy = GetWorld()->SpawnActorDeferred<AEnemyBase_New>(
        EnemyClass,
        FTransform::Identity,
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    );

    if (SpawnedEnemy)
    {
        // 3. 注入生命周期与战斗参数
        SpawnedEnemy->EnemyIndex = TargetEnemyIndex;
        SpawnedEnemy->EnemyMeshIndex = TargetEnemyMeshIndex;
        SpawnedEnemy->MaxHealth = TempMaxHP;
        SpawnedEnemy->Health = TempMaxHP;
        SpawnedEnemy->MyBindingTag = TempSequenceTag;
        SpawnedEnemy->RewardType = TempReward;

        // 4. 完成生成：触发 BeginPlay 并开始网络同步
        UGameplayStatics::FinishSpawningActor(SpawnedEnemy, FTransform::Identity);

        // 5. 立即尝试执行序列同步绑定
        // 必须放置于 FinishSpawningActor 之后，确保组件已经完全注册并生效
        if (SpawnedEnemy->SyncComp)
        {
            AVRGunGameStateBase* GS = Cast<AVRGunGameStateBase>(GetWorld()->GetGameState());
            if (GS && GS->LocalSequenceActor)
            {
                // 将刚出生的怪物绑定到全局统一的本地关卡序列中
                SpawnedEnemy->SyncComp->LocalInitAndBind(GS->LocalSequenceActor, GS->ActiveSequenceStartTime, TempSequenceTag);
            }
        }
    }

    CurrentSpawnNumber++;

    // 6. 如果当前生成数量达到目标总数，清理定时器停止调度
    if (CurrentSpawnNumber >= ActiveSpawnData.SpawnNumber)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_Spawn);
    }
}

#pragma endregion