
// 文件概述：该组件负责武器的生成、绑定、开火判定、服务器裁决与表现同步。
// 设计重点：网络权威（服务器）负责数据与伤害裁决；客户端负责本地表现与低开销的脑补逻辑。
// 注意事项：定时器、RepNotify、RPC、以及对 Listen Server 的特殊处理都需要谨慎使用。

#include "VRGun/Component/VRWeaponComponent.h"
#include "VRGun/WeaponActor/VRWeaponActor.h"
#include "VRGun/VRCharacter_Gun.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "VRGun/Prop/VRAimActor.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Components/SphereComponent.h"
#include "VRGun/Prop/VRDamageNumberActor.h"
#include "VRGun/GameInstance/VR_GameInstance.h"
#include "VRGun/Pawn/DirectorPawn.h"
#include "EngineUtils.h"

#pragma region /* 初始化与网络属性 */

/*
 * 构造函数
 * - 关闭每帧 Tick（默认不需要）
 * - 开启组件级别的网络复制以保证武器逻辑在客户端/服务器间同步
 */
UVRWeaponComponent::UVRWeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true); // 武器组件负责核心战斗逻辑，必须开启网络同步
}

/*
 * 网络属性注册
 * - 将需要复制的 UPROPERTY 注册到 ActorChannel 生命周期中
 * - bIsFiring_Net 使用条件复制以避免在拥有者上重复处理
 */
void UVRWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UVRWeaponComponent, CurrentStats);         // 同步当前武器的实时属性（如伤害提升后）
    DOREPLIFETIME(UVRWeaponComponent, Rep_WeaponEquipInfo);  // 同步当前的武器装备信息（用于驱动两端生成实体）
    DOREPLIFETIME_CONDITION(UVRWeaponComponent, bIsFiring_Net, COND_SkipOwner);
}

/*
 * BeginPlay 初始化
 * - 仅服务器读取并初始化武器数据表
 * - 缓存宿主角色与网格体引用以供高频开火逻辑使用，减少 Cast 开销
 */
void UVRWeaponComponent::BeginPlay()
{
    Super::BeginPlay();

    // 仅服务器有权初始化基础武器数据
    if (GetOwner()->HasAuthority())
    {
        InitWeaponData();
    }

    // 缓存宿主角色及其网格体引用，避免在后续高频的开火逻辑中反复 Cast 产生性能开销
    CachedCharacter = Cast<AVRCharacter_Gun>(GetOwner());
    if (CachedCharacter)
    {
        CachedGunMesh1P = CachedCharacter->GunMesh_1P;
        CachedMesh3P = CachedCharacter->GetMesh();
    }
}

#pragma endregion


#pragma region /* 装备与生成系统 */

/**
 * RequestEquipWeapon
 * - 本地或服务器均可调用（客户端会发 RPC 到服务器）
 * - 支持限时武器：把被顶替的永久武器备份以便到期恢复
 * - 服务器负责实际写入 Rep_WeaponEquipInfo 并启动/清理定时器
 */
void UVRWeaponComponent::RequestEquipWeapon(TSubclassOf<AVRWeaponActor> NewWeaponClass, FName NewRowName, float Duration)
{
    if (!NewWeaponClass) return;

    FWeaponEquipInfo NewInfo;
    NewInfo.WeaponClass = NewWeaponClass;
    NewInfo.WeaponRowName = NewRowName;
    NewInfo.Duration = Duration; // 记录该武器的持续时间

    if (GetOwner()->HasAuthority())
    {
        // 【备份逻辑】：如果要换上的是限时武器（如吃了道具），且手里当前是永久武器，则将永久武器备份
        if (NewInfo.Duration > 0.0f && Rep_WeaponEquipInfo.Duration <= 0.0f)
        {
            if (Rep_WeaponEquipInfo.WeaponClass != nullptr)
            {
                SavedDefaultWeaponInfo = Rep_WeaponEquipInfo;
                bHasSavedDefaultWeapon = true;
            }
        }

        Rep_WeaponEquipInfo = NewInfo;
        OnRep_WeaponEquipInfo(); // 服务器直接手动触发装备更新逻辑

        // 【定时器逻辑】：清理旧的倒计时，如果新武器是限时的，则开启自动卸载定时器
        GetWorld()->GetTimerManager().ClearTimer(WeaponDurationTimerHandle);
        if (NewInfo.Duration > 0.0f)
        {
            GetWorld()->GetTimerManager().SetTimer(WeaponDurationTimerHandle, this, &UVRWeaponComponent::OnWeaponDurationExpired, NewInfo.Duration, false);
        }
    }
    else
    {
        // 客户端无权直接修改，向服务器发送 RPC 请求
        Server_RequestEquipWeapon(NewInfo);
    }
}

/*
 * Server RPC for RequestEquipWeapon
 * - Validate 返回 true（可根据需要扩展权限检查）
 * - Implementation 在服务器上重复本地逻辑（备份 + 复制 + 定时器）
 */
bool UVRWeaponComponent::Server_RequestEquipWeapon_Validate(FWeaponEquipInfo NewInfo) { return true; }

void UVRWeaponComponent::Server_RequestEquipWeapon_Implementation(FWeaponEquipInfo NewInfo)
{
    // 服务器端执行完全相同的备份与定时器逻辑
    if (NewInfo.Duration > 0.0f && Rep_WeaponEquipInfo.Duration <= 0.0f)
    {
        if (Rep_WeaponEquipInfo.WeaponClass != nullptr)
        {
            SavedDefaultWeaponInfo = Rep_WeaponEquipInfo;
            bHasSavedDefaultWeapon = true;
        }
    }

    Rep_WeaponEquipInfo = NewInfo;
    OnRep_WeaponEquipInfo();

    GetWorld()->GetTimerManager().ClearTimer(WeaponDurationTimerHandle);
    if (NewInfo.Duration > 0.0f)
    {
        GetWorld()->GetTimerManager().SetTimer(WeaponDurationTimerHandle, this, &UVRWeaponComponent::OnWeaponDurationExpired, NewInfo.Duration, false);
    }
}

/**
 * RepNotify：当客户端收到新的装备信息时触发，执行本地实体生成与绑定
 * - Replication 会在客户端触发此函数，服务器可直接调用以保持一致性
 */
void UVRWeaponComponent::OnRep_WeaponEquipInfo()
{
    PerformWeaponSpawnAndBind();
}

/**
 * PerformWeaponSpawnAndBind
 * - 核心：销毁旧武器实体，生成新武器实体，并且吸附到角色骨骼
 * - 注意：换枪时必须 StopFire() 避免定时器或状态残留
 */
void UVRWeaponComponent::PerformWeaponSpawnAndBind()
{
    StopFire(); // 换枪时必须打断开火状态

   

    if (EquippedWeapon)
    {
        EquippedWeapon->Destroy();
        EquippedWeapon = nullptr;
    }

    if (!Rep_WeaponEquipInfo.WeaponClass) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    EquippedWeapon = GetWorld()->SpawnActor<AVRWeaponActor>(
        Rep_WeaponEquipInfo.WeaponClass,
        GetOwner()->GetActorLocation(),
        GetOwner()->GetActorRotation(),
        SpawnParams
    );

    // 将视觉表现实体吸附到角色的 1P 和 3P 骨骼插槽上
    if (EquippedWeapon)
    {
        BindWeaponMeshesToCharacter(TEXT("GripPoint"), TEXT("GripPoint"));
    }

    // 服务器负责解析数据表，应用新武器的数值属性
    if (GetOwner()->HasAuthority())
    {
        WeaponRowName = Rep_WeaponEquipInfo.WeaponRowName;
        InitWeaponData();
    }
}

/*
 * BindWeaponMeshesToCharacter
 * - 防御性编程：处理 OnRep 比 BeginPlay 更早的情况（因此一开始可能没有缓存）
 * - 将武器的 1P/3P Mesh 附着到角色对应插槽，使用 SnapToTargetNotIncludingScale 保持位置/旋转一致
 */
void UVRWeaponComponent::BindWeaponMeshesToCharacter(FName SocketName1P, FName SocketName3P)
{
    // 防御性设计：防范网络同步极快，导致 OnRep 早于 BeginPlay 执行，从而发生空指针崩溃
    if (!CachedCharacter)
    {
        CachedCharacter = Cast<AVRCharacter_Gun>(GetOwner());
        if (CachedCharacter)
        {
            CachedGunMesh1P = CachedCharacter->GunMesh_1P;
            CachedMesh3P = CachedCharacter->GetMesh();
        }
    }

    if (!EquippedWeapon || !CachedCharacter) return;

    if (EquippedWeapon->WeaponMesh_1P && CachedGunMesh1P)
    {
        EquippedWeapon->WeaponMesh_1P->AttachToComponent(CachedGunMesh1P, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName1P);
    }

    if (EquippedWeapon->WeaponMesh_3P && CachedMesh3P)
    {
        EquippedWeapon->WeaponMesh_3P->AttachToComponent(CachedMesh3P, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName3P);
    }
}

void UVRWeaponComponent::SetCanFire(bool bEnable)
{
    bCanFire = bEnable;
    if (!bCanFire)
    {
        StopFire(); // 锁死瞬间，立刻掐断开火定时器和网络状态
    }
}

/**
 * GrantWeaponReward
 * - 用于击杀/任务奖励直接发武器
 * - 通过映射表查找配置并调用 RequestEquipWeapon（走正常的服务器验证路径）
 */
void UVRWeaponComponent::GrantWeaponReward(EWeaponRewardType RewardType)
{
    if (RewardWeaponMap.Contains(RewardType))
    {
        FWeaponEquipInfo RewardInfo = RewardWeaponMap[RewardType];
        RequestEquipWeapon(RewardInfo.WeaponClass, RewardInfo.WeaponRowName, RewardInfo.Duration);
    }
}

/**
 * OnWeaponDurationExpired
 * - 限时武器到期处理：恢复之前备份的永久武器
 * - 只有服务器拥有发枪权限
 */
void UVRWeaponComponent::OnWeaponDurationExpired()
{
    // 强制切回备份的永久默认武器 (Duration 设回 0.0f)
    if (GetOwner()->HasAuthority() && bHasSavedDefaultWeapon)
    {
        RequestEquipWeapon(SavedDefaultWeaponInfo.WeaponClass, SavedDefaultWeaponInfo.WeaponRowName, 0.0f);
    }
}

/*
 * EquipInitialWeapon
 * - 服务器在玩家进入时调用：根据投币/地图决定发什么武器
 * - 兼容彩带枪（未投币）以及地图专属武器与兜底武器
 */
void UVRWeaponComponent::EquipInitialWeapon()
{
    // 只有服务器有资格发枪
    if (!GetOwner()->HasAuthority()) return;

    // 获取角色和 GameInstance
    AVRCharacter_Gun* OwnerChar = Cast<AVRCharacter_Gun>(GetOwner());
    UVR_GameInstance* GI = Cast<UVR_GameInstance>(UGameplayStatics::GetGameInstance(this));
    if (!OwnerChar || !GI) return;

    // 判定该玩家是否已投币
    bool bHasPaid = (OwnerChar->AssignedPlayerIndex == 0) ? GI->bPlayer0_Paid : GI->bPlayer1_Paid;

    if (!bHasPaid)
    {
        // 没投币：强制发彩带枪
        RequestEquipWeapon(UnpaidRibbonWeapon.WeaponClass, UnpaidRibbonWeapon.WeaponRowName, 0.0f);
        UE_LOG(LogTemp, Warning, TEXT("[Weapon] 玩家 %d 未投币，已装备彩带枪！"), OwnerChar->AssignedPlayerIndex);
    }
    else
    {
        // 已投币：获取当前地图名并去 Map 中查找
        FString CurrentMapName = UGameplayStatics::GetCurrentLevelName(this, true);

        if (MapDefaultWeaponMap.Contains(CurrentMapName))
        {
            FWeaponEquipInfo MapWeapon = MapDefaultWeaponMap[CurrentMapName];
            RequestEquipWeapon(MapWeapon.WeaponClass, MapWeapon.WeaponRowName, 0.0f);
            UE_LOG(LogTemp, Warning, TEXT("[Weapon] 玩家 %d 已投币，根据地图 %s 装备了专属武器！"), OwnerChar->AssignedPlayerIndex, *CurrentMapName);
        }
        else
        {
            // 如果地图名没在 Map 里配置，发兜底武器
            RequestEquipWeapon(FallbackDefaultWeapon.WeaponClass, FallbackDefaultWeapon.WeaponRowName, 0.0f);
            UE_LOG(LogTemp, Warning, TEXT("[Weapon] 玩家 %d 已投币，地图 %s 未配置，发放兜底主武器！"), OwnerChar->AssignedPlayerIndex, *CurrentMapName);
        }
    }
}
#pragma endregion


#pragma region /* 属性与工具方法 */

/*
 * InitWeaponData
 * - 从 DataTable 读取武器数值表（服务器负责），并把 CurrentStats 同步/广播到客户端
 * - 读取后立即更新本地准星（客户端表现）
 */
void UVRWeaponComponent::InitWeaponData()
{
    if (WeaponDataTable && !WeaponRowName.IsNone())
    {
        FWeaponStatData* RowData = WeaponDataTable->FindRow<FWeaponStatData>(WeaponRowName, TEXT("WeaponInit"));
        if (RowData)
        {
            CurrentStats = *RowData;
            UpdateLocalCrosshair(); // 数据加载后立即更新准星 UI
        }
    }
}

/*
 * UpgradeDamage
 * - 服务器调用以修改当前武器的伤害（例如道具/增益）
 * - 只允许权威端修改数据，复制后客户端会通过 OnRep_CurrentStats 同步表现
 */
void UVRWeaponComponent::UpgradeDamage(float ExtraDamage)
{
    if (GetOwner()->HasAuthority()) CurrentStats.Damage += ExtraDamage;
}

/**
 * GetHitType
 * - 将被击中的 Actor 的标签转换为表现所需的受击类型枚举
 * - 例如 Enemy/Floor/Water 等，用于决定特效、音效或命中特效
 */
EVRHitType UVRWeaponComponent::GetHitType(AActor* InActor)
{
    if (!IsValid(InActor)) return EVRHitType::None;

    if (InActor->ActorHasTag(TEXT("Enemy"))) return EVRHitType::Enemy;
    if (InActor->ActorHasTag(TEXT("Floor"))) return EVRHitType::Floor;
    if (InActor->ActorHasTag(TEXT("Water"))) return EVRHitType::Water;
    if (InActor->ActorHasTag(TEXT("Rock")))  return EVRHitType::Rock;
    if (InActor->ActorHasTag(TEXT("Wood")))  return EVRHitType::Wood;
    if (InActor->ActorHasTag(TEXT("QTE")))   return EVRHitType::QTE;
    if (InActor->ActorHasTag(TEXT("Dirt")))  return EVRHitType::Dirt;
    if (InActor->ActorHasTag(TEXT("Metal"))) return EVRHitType::Metal;

    return EVRHitType::Other;
}

// ==================================================
// 辅助函数 1：返回统一的射线碰撞通道设置
// - 将常用的碰撞通道集中到一个函数，便于维护与复用
// ==================================================
FCollisionObjectQueryParams UVRWeaponComponent::GetWeaponObjectQueryParams() const
{
    FCollisionObjectQueryParams ObjectQueryParams;
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_ENEMY);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_WOOD);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_PROP);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_ROCK);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_FLOOR);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_WATER);
    ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_DIRT);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_METAL);
    return ObjectQueryParams;
}

// ==================================================
// 辅助函数 2：统一生成 3D 伤害跳字
// - 只在客户端生成视觉跳字
// - 对连锁闪电枪做衰减显示处理
// - 跳字为仅主人可见，并且对导播隐藏，避免额外渲染
// ==================================================
void UVRWeaponComponent::SpawnDamageNumbers(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const FVector& StartLoc)
{
    if (!DamageNumberClass) return;

    for (int32 i = 0; i < HitActors.Num(); i++)
    {
        AActor* HitActor = HitActors[i];

        // 只有打中怪物才跳字 (空指针或打中墙壁则跳过)
        if (HitActor && HitActor->ActorHasTag(TEXT("Enemy")))
        {
            FVector HitLoc = TargetLocations.IsValidIndex(i) ? TargetLocations[i] : FVector::ZeroVector;
            float DistToPlayer = FVector::Distance(StartLoc, HitLoc);

            if (DistToPlayer >= MinDistanceToSpawnDamage)
            {
                // 智能判定：只有连锁闪电枪才使用 i (弹射次数) 衰减伤害
                // 霰弹枪/切割枪的 i 只是射线编号，不衰减，保持原伤害！
                float DisplayDamage = CurrentStats.Damage;
                if (CurrentStats.WeaponType == EVRWeaponType::ChainGun)
                {
                    DisplayDamage *= FMath::Pow(CurrentStats.ChainDamageFalloff, (float)i);
                }

                FActorSpawnParameters SpawnParams;
                SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

                AVRDamageNumberActor* DamageActor = GetWorld()->SpawnActor<AVRDamageNumberActor>(
                    DamageNumberClass, HitLoc, FRotator::ZeroRotator, SpawnParams);

                if (DamageActor)
                {
                    DamageActor->InitDamageText(DisplayDamage, HitLoc);
                    // 第一道锁：主人专属
                    DamageActor->SetOwner(GetOwner()); // 确保准星认你做主人
                    TArray<UPrimitiveComponent*> Comps;
                    DamageActor->GetComponents<UPrimitiveComponent>(Comps);
                    for (UPrimitiveComponent* Comp : Comps)
                    {
                        Comp->SetOnlyOwnerSee(true);
                    }

                    // 🌟 问题 3 修复：第二道锁：打入导播黑名单，防万一！
                    if (GetWorld())
                    {
                        for (TActorIterator<ADirectorPawn> It(GetWorld()); It; ++It)
                        {
                            if (ADirectorPawn* Director = *It)
                            {
                                // 导播相机渲染时将直接跳过准星 Actor！
                                Director->HideActorFromDirector(DamageActor);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

// ==================================================
// 辅助函数 3：统一执行本地表现与服务器上报
// - 本地触发武器视觉、角色动画，然后把采集到的数据发给服务器裁决
// - 保证客户端感觉即时，同时服务器负责最终伤害判定和安全性
// ==================================================
void UVRWeaponComponent::FinalizeFire(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<FVector>& ShotDirections, const TArray<FName>& HitBoneNames)
{
    // 1. 触发本地枪口/子弹视觉表现
    EquippedWeapon->FireVisuals(TargetLocations, TargetNormals, LastHitTypes, true, CurrentStats.WeaponType);

    // 2. 播放 1P 角色手部后坐力动画
    if (CurrentStats.CharacterFireMontage && CachedGunMesh1P)
    {
        if (UAnimInstance* AnimInst = CachedGunMesh1P->GetAnimInstance())
        {
            AnimInst->Montage_Play(CurrentStats.CharacterFireMontage);
        }
    }

    //本地播放开火音效
    
    // 3. 将所有收集到的数据提交给服务器裁决
    Server_ProcessHits(HitActors, TargetLocations, TargetNormals, LastHitTypes, ShotDirections, HitBoneNames);
}
#pragma endregion


#pragma region /* 开火执行与射击判定 */

/*
 * StartFire
 * - 只有本地主控的 Pawn 可以调用开火（防止 AI 或远端直接操作）
 * - 通知 LocalVRAimActor 进入开火状态（准星扩张）
 * - 向服务器上报持续开火状态（用于远端脑补）
 * - 若武器为连发（FireRate>0），启动定时器周期性调用 ExecuteFire
 */
void UVRWeaponComponent::StartFire()
{
    if (!bCanFire) return;

    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    // 只有本地主控玩家有权发起射击逻辑
    if (!OwnerPawn || !OwnerPawn->IsLocallyControlled() || !EquippedWeapon) return;

    // 通知 3D 准星进入开火扩张状态
    if (CachedCharacter && CachedCharacter->LocalVRAimActor)
    {
        CachedCharacter->LocalVRAimActor->SetFiringState(true);
    }

    //向服务器上报持续开火状态
    Server_SetFiringState(true);

    ExecuteFire();

    // 如果是连发武器，开启自动开火定时器
    if (CurrentStats.FireRate > 0.0f)
    {
        float TimeBetweenShots = 1.0f / CurrentStats.FireRate;
        GetWorld()->GetTimerManager().SetTimer(FireTimerHandle, this, &UVRWeaponComponent::ExecuteFire, TimeBetweenShots, true);
    }
}

/*
 * StopFire
 * - 清理本地开火定时器与远端脑补定时器
 * - 恢复准星状态
 * - 向服务器上报停火（服务器会设置 bIsFiring_Net）
 */
void UVRWeaponComponent::StopFire()
{
    GetWorld()->GetTimerManager().ClearTimer(FireTimerHandle);

    // 【关键修复】：换枪时，强制掐断远端 3P 的自动开火表现定时器！
    GetWorld()->GetTimerManager().ClearTimer(AutoFireVisualTimerHandle);

    // 通知 3D 准星收缩恢复
    if (CachedCharacter && CachedCharacter->LocalVRAimActor)
    {
        CachedCharacter->LocalVRAimActor->SetFiringState(false);
    }
    // 只有权威端或本地主控玩家，才有资格向服务器上报停火网络状态
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (GetOwner()->HasAuthority() || (OwnerPawn && OwnerPawn->IsLocallyControlled()))
    {
        Server_SetFiringState(false);
    }
}

/*
 * ExecuteFire
 * - 根据武器类型分发到不同的射线算法
 * - 触发准星单次脉冲动画以增强反馈
 */
void UVRWeaponComponent::ExecuteFire()
{
    if (!EquippedWeapon) return;

    // 触发准星单次脉冲动画
    if (CachedCharacter && CachedCharacter->LocalVRAimActor)
    {
        CachedCharacter->LocalVRAimActor->PlayFirePulse(CurrentStats.FireRate);
    }

    // 根据武器类型分发具体的射线算法
    switch (CurrentStats.WeaponType)
    {
    case EVRWeaponType::Normal:
    case EVRWeaponType::Shotgun:
        PerformHitscan_NormalAndShotgun();
        break;
    case EVRWeaponType::HorizontalLine:
        PerformHitscan_HorizontalLine();
        break;
    case EVRWeaponType::ChainGun:
        PerformHitscan_ChainGun();
        break;
    case EVRWeaponType::Ribbon:          // <--- 新增分支
        PerformHitscan_Ribbon();
        break;
    }
}

/*
 * GetShootOriginAndDirection
 * - 默认从摄像机发射（第一人称/视角优先）
 * - 退而求其次使用 Actor 的位置与朝向
 */
void UVRWeaponComponent::GetShootOriginAndDirection(FVector& OutStartLoc, FVector& OutForwardDir, FVector& OutUpDir)
{
    // 默认的自由移动模式：从摄像机（眼睛）发射
    if (UCameraComponent* CameraComp = GetOwner()->FindComponentByClass<UCameraComponent>())
    {
        OutStartLoc = CameraComp->GetComponentLocation();
        OutForwardDir = CameraComp->GetForwardVector();
        OutUpDir = CameraComp->GetUpVector();
    }
    else
    {
        OutStartLoc = GetOwner()->GetActorLocation();
        OutForwardDir = GetOwner()->GetActorForwardVector();
        OutUpDir = GetOwner()->GetActorUpVector();
    }
}

/**
 * PerformHitscan_NormalAndShotgun
 * - 圆锥形散射实现（霰弹/普通子弹复用）
 * - 为每根射线记录命中对象与信息，最后统一调用 SpawnDamageNumbers 和 FinalizeFire
 */
void UVRWeaponComponent::PerformHitscan_NormalAndShotgun()
{
    FVector StartLoc, ForwardDir, UpDir;
    GetShootOriginAndDirection(StartLoc, ForwardDir, UpDir);

    int32 TraceCount = FMath::Max(1, CurrentStats.TraceCount);
    float Range = CurrentStats.FireRange > 0.0f ? CurrentStats.FireRange : 10000.f;

    TArray<AActor*> HitActors;
    TArray<FVector> TargetLocations;
    TArray<FVector> TargetNormals;
    TArray<FVector> ShotDirections;
    TArray<FName> HitBoneNames;
    LastHitTypes.Empty();

    // 🌟 一行代码搞定几十行的通道配置！
    FCollisionObjectQueryParams ObjectQueryParams = GetWeaponObjectQueryParams();

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    // 核心算法：循环发射射线
    for (int32 i = 0; i < TraceCount; i++)
    {
        FVector ShootDir = FMath::VRandCone(ForwardDir, FMath::DegreesToRadians(CurrentStats.SpreadAngle));
        FVector EndLoc = StartLoc + (ShootDir * Range);
        FHitResult HitResult;

        if (GetWorld()->LineTraceSingleByObjectType(HitResult, StartLoc, EndLoc, ObjectQueryParams, Params))
        {
            AActor* HitActor = HitResult.GetActor();

            // 数据装填 (用三元运算符让代码更简洁)
            HitActors.Add((HitActor && HitActor->GetIsReplicated()) ? HitActor : nullptr);
            TargetLocations.Add(HitResult.ImpactPoint);
            TargetNormals.Add(HitResult.ImpactNormal);
            ShotDirections.Add(ShootDir);
            HitBoneNames.Add(HitResult.BoneName);
            LastHitTypes.Add(GetHitType(HitActor));
        }
        else
        {
            HitActors.Add(nullptr);
            TargetLocations.Add(EndLoc);
            TargetNormals.Add(-ShootDir);
            ShotDirections.Add(ShootDir);
            HitBoneNames.Add(NAME_None);
            LastHitTypes.Add(EVRHitType::None);
        }
    }

    // 🌟 一行代码搞定几十行的伤害跳字生成！
    SpawnDamageNumbers(HitActors, TargetLocations, StartLoc);

    // 🌟 一行代码搞定特效、动画和发送给服务器！
    FinalizeFire(HitActors, TargetLocations, TargetNormals, ShotDirections, HitBoneNames);
}

/**
 * PerformHitscan_HorizontalLine
 * - 固定夹角的一字排开散射（保证中线有一根射线）
 * - 通过 RotateAngleAxis 围绕 Up 向量做平面旋转
 */
void UVRWeaponComponent::PerformHitscan_HorizontalLine()
{
    FVector StartLoc, ForwardDir, UpDir;
    GetShootOriginAndDirection(StartLoc, ForwardDir, UpDir);

    // 【架构要求】：强制将射线数量修正为奇数，保证永远有一根正中心的主射线。
    int32 TraceCount = FMath::Max(1, CurrentStats.TraceCount);
    if (TraceCount % 2 == 0) TraceCount += 1;

    float Range = CurrentStats.FireRange > 0.0f ? CurrentStats.FireRange : 10000.f;
    float AngleGap = CurrentStats.HorizontalSpreadGap;

    TArray<AActor*> HitActors;
    TArray<FVector> TargetLocations;
    TArray<FVector> TargetNormals;
    TArray<FVector> ShotDirections;
    TArray<FName> HitBoneNames;
    LastHitTypes.Empty();

    // 🌟 1. 一行代码获取所有碰撞通道配置
    FCollisionObjectQueryParams ObjectQueryParams = GetWeaponObjectQueryParams();

    // 计算总散射跨度和起始左侧角度
    float TotalAngle = (TraceCount - 1) * AngleGap;
    float StartAngle = -TotalAngle / 2.0f;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    for (int32 i = 0; i < TraceCount; i++)
    {
        // 计算当前这根射线相对中心点的偏移角度
        float CurrentAngle = StartAngle + (i * AngleGap);

        // 利用 RotateAngleAxis 将正前方向量围绕 Z 轴(向上向量)进行平面旋转
        FVector ShootDir = ForwardDir.RotateAngleAxis(CurrentAngle, UpDir);
        FVector EndLoc = StartLoc + (ShootDir * Range);
        FHitResult HitResult;

        if (GetWorld()->LineTraceSingleByObjectType(HitResult, StartLoc, EndLoc, ObjectQueryParams, Params))
        {
            AActor* HitActor = HitResult.GetActor();

            // 数据装填 (使用三元运算符极简处理空指针判定)
            HitActors.Add((HitActor && HitActor->GetIsReplicated()) ? HitActor : nullptr);
            TargetLocations.Add(HitResult.ImpactPoint);
            TargetNormals.Add(HitResult.ImpactNormal);
            ShotDirections.Add(ShootDir);
            HitBoneNames.Add(HitResult.BoneName);
            LastHitTypes.Add(GetHitType(HitActor));
        }
        else
        {
            HitActors.Add(nullptr);
            TargetLocations.Add(EndLoc);
            TargetNormals.Add(-ShootDir);
            ShotDirections.Add(ShootDir);
            HitBoneNames.Add(NAME_None);
            LastHitTypes.Add(EVRHitType::None);
        }
    }

    // 🌟 2. 统一处理本地 3D 伤害跳字生成
    SpawnDamageNumbers(HitActors, TargetLocations, StartLoc);

    // 🌟 3. 统一执行本地视觉特效、后坐力动画与服务器 RPC 上报
    FinalizeFire(HitActors, TargetLocations, TargetNormals, ShotDirections, HitBoneNames);
}

/**
 * PerformHitscan_ChainGun
 * - 连锁闪电：先用 Sweep 找到首目标，然后做多次 Overlap 查找跳跃目标（去重已被击中的实体）
 * - 处理弹射次数、衰减与跳跃圆心更新
 */
void UVRWeaponComponent::PerformHitscan_ChainGun()
{
    FVector StartLoc, ForwardDir, UpDir;
    GetShootOriginAndDirection(StartLoc, ForwardDir, UpDir);

    float Range = CurrentStats.FireRange > 0.0f ? CurrentStats.FireRange : 10000.f;

    TArray<AActor*> HitActors;
    TArray<FVector> TargetLocations;
    TArray<FVector> TargetNormals;
    TArray<FVector> ShotDirections;
    TArray<FName> HitBoneNames;
    LastHitTypes.Empty();

    // 🌟 1. 一行代码获取所有碰撞通道配置
    FCollisionObjectQueryParams ObjectQueryParams = GetWeaponObjectQueryParams();

    FVector EndLoc = StartLoc + ForwardDir * Range;
    FHitResult FirstHit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    FCollisionShape SweepShape = FCollisionShape::MakeSphere(CurrentStats.ChainPrimarySweepRadius);

    // 发射主射线，寻找第一个目标
    if (GetWorld()->SweepSingleByObjectType(FirstHit, StartLoc, EndLoc, FQuat::Identity, ObjectQueryParams, SweepShape, Params))
    {
        AActor* FirstHitActor = FirstHit.GetActor();

        TargetLocations.Add(FirstHit.ImpactPoint);
        TargetNormals.Add(FirstHit.ImpactNormal);
        ShotDirections.Add(ForwardDir);
        HitBoneNames.Add(FirstHit.BoneName);
        LastHitTypes.Add(GetHitType(FirstHitActor));
        HitActors.Add((FirstHitActor && FirstHitActor->GetIsReplicated()) ? FirstHitActor : nullptr);

        // 【核心机制】：如果首击命中怪物，启动空间跳跃索敌机制
        if (FirstHitActor && FirstHitActor->ActorHasTag(TEXT("Enemy")))
        {
            FVector CurrentSearchCenter = FirstHit.ImpactPoint;

            // 依据配置的最大跳跃次数进行循环扫描
            for (int32 BounceIdx = 0; BounceIdx < CurrentStats.ChainMaxBounces; BounceIdx++)
            {
                TArray<FOverlapResult> OverlapResults;
                FCollisionShape SphereShape = FCollisionShape::MakeSphere(CurrentStats.ChainBounceRadius);

                FCollisionObjectQueryParams EnemyQuery;
                EnemyQuery.AddObjectTypesToQuery(COLLISION_ENEMY);

                // 排除自己，以及已经被电过的怪物
                FCollisionQueryParams SphereParams;
                SphereParams.AddIgnoredActor(GetOwner());
                SphereParams.AddIgnoredActors(HitActors);

                bool bFound = GetWorld()->OverlapMultiByObjectType(OverlapResults, CurrentSearchCenter, FQuat::Identity, EnemyQuery, SphereShape, SphereParams);

                if (bFound)
                {
                    AActor* ClosestEnemy = nullptr;
                    float MinDistance = 999999.0f;
                    FVector ClosestLocation = FVector::ZeroVector;

                    // 找出扫描范围内距离当前跳跃点最近的怪物
                    for (const FOverlapResult& Overlap : OverlapResults)
                    {
                        AActor* FoundActor = Overlap.GetActor();
                        if (FoundActor && FoundActor->ActorHasTag(TEXT("Enemy")))
                        {
                            float Dist = FVector::Distance(CurrentSearchCenter, FoundActor->GetActorLocation());
                            if (Dist < MinDistance)
                            {
                                MinDistance = Dist;
                                ClosestEnemy = FoundActor;
                                ClosestLocation = FoundActor->GetActorLocation();
                            }
                        }
                    }

                    if (ClosestEnemy)
                    {
                        // 算出这一段闪电跳跃的方向
                        FVector JumpDir = (ClosestLocation - CurrentSearchCenter).GetSafeNormal();

                        HitActors.Add(ClosestEnemy);
                        TargetLocations.Add(ClosestLocation);
                        TargetNormals.Add(-JumpDir);
                        ShotDirections.Add(JumpDir);
                        HitBoneNames.Add(NAME_None);
                        LastHitTypes.Add(EVRHitType::Enemy);

                        // 更新圆心，准备以这个新目标为原点进行下一次寻敌
                        CurrentSearchCenter = ClosestLocation;
                    }
                    else break;
                }
                else break; // 周围没怪了，跳跃终止
            }
        }
    }
    else
    {
        TargetLocations.Add(EndLoc);
        TargetNormals.Add(-ForwardDir);
        HitActors.Add(nullptr);
        ShotDirections.Add(ForwardDir);
        HitBoneNames.Add(NAME_None);
        LastHitTypes.Add(EVRHitType::None);
    }

    // 🌟 2. 统一处理本地 3D 伤害跳字生成 (辅助函数内部已经做好了电枪衰减计算)
    SpawnDamageNumbers(HitActors, TargetLocations, StartLoc);

    // 🌟 3. 统一执行本地视觉特效、后坐力动画与服务器 RPC 上报
    FinalizeFire(HitActors, TargetLocations, TargetNormals, ShotDirections, HitBoneNames);
}

/*
 * PerformHitscan_Ribbon
 * - 彩带枪为纯视觉武器：不做物理射线判定以节约性能
 * - 生成一个伪目标供表现层（FireVisuals）播放喷射效果
 * - 传给服务器空的 HitActors 保证不会产生伤害
 */
void UVRWeaponComponent::PerformHitscan_Ribbon()
{
    FVector StartLoc, ForwardDir, UpDir;
    GetShootOriginAndDirection(StartLoc, ForwardDir, UpDir);

    // 【核心优化】：彩带枪绝对不调用引擎的 LineTrace 射线，0 物理消耗！
    TArray<AActor*> HitActors;        // 空名单，代表没打中任何人
    TArray<FVector> TargetLocations;
    TArray<FVector> TargetNormals;
    TArray<FVector> ShotDirections;
    TArray<FName> HitBoneNames;

    // 伪造一个目标点（比如前方5米），仅用于让视觉系统知道彩带朝哪里喷
    FVector FakeEndLoc = StartLoc + (ForwardDir * 500.0f);

    TargetLocations.Add(FakeEndLoc);
    TargetNormals.Add(-ForwardDir);
    ShotDirections.Add(ForwardDir);
    HitBoneNames.Add(NAME_None);
    LastHitTypes.Empty();
    LastHitTypes.Add(EVRHitType::None);

    // 完美复用你现有的架构！
    // 传给 FinalizeFire 后，服务器收到空的 HitActors，会循环 0 次造成 0 伤害，
    // 但依然会触发 Multicast_PlayFireEffects，让全网所有人看到这把枪喷彩带的 3P 动画！
    FinalizeFire(HitActors, TargetLocations, TargetNormals, ShotDirections, HitBoneNames);
}
#pragma endregion


#pragma region /* 服务器伤害裁决与多播表现 */

/*
 * Server_ProcessHits
 * - 服务器对客户端提交的命中数据进行最终校验与伤害应用（裁决）
 * - 此处构造 FakeHit 用于调用 ApplyPointDamage，使 Actor 能接收到标准伤害事件
 * - ChainGun 的伤害随弹射次数衰减
 * - 最后根据射速决定是否走精确 Multicast（慢速武器使用精确多播，快速武器交由客户端脑补）
 */
bool UVRWeaponComponent::Server_ProcessHits_Validate(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, const TArray<FVector>& ShotDirections, const TArray<FName>& HitBoneNames)
{
    return true;
}

void UVRWeaponComponent::Server_ProcessHits_Implementation(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, const TArray<FVector>& ShotDirections, const TArray<FName>& HitBoneNames)
{
    // 服务器依次对命中的目标进行伤害裁决
    for (int32 i = 0; i < HitActors.Num(); i++)
    {
        AActor* HitActor = HitActors[i];
        if (IsValid(HitActor))
        {
            // 重构 FHitResult，提供给 Unreal 的应用伤害框架
            FHitResult FakeHit;
            FakeHit.ImpactPoint = TargetLocations.IsValidIndex(i) ? TargetLocations[i] : FVector::ZeroVector;
            FakeHit.ImpactNormal = TargetNormals.IsValidIndex(i) ? TargetNormals[i] : FVector::UpVector;
            FakeHit.BoneName = HitBoneNames.IsValidIndex(i) ? HitBoneNames[i] : NAME_None;

            FVector ShotDir = ShotDirections.IsValidIndex(i) ? ShotDirections[i] : FVector::ForwardVector;

            // 计算当前目标的伤害（处理电枪的逐级递减衰减）
            float AppliedDamage = CurrentStats.Damage;
            if (CurrentStats.WeaponType == EVRWeaponType::ChainGun)
            {
                // i=0 承受全额，i=1 衰减一次，i=2 衰减两次...
                AppliedDamage *= FMath::Pow(CurrentStats.ChainDamageFalloff, (float)i);
            }

            // 根据目标 Tag 区分是有效伤害目标还是仅需物理反馈(如场景物品)
            if (HitActor->ActorHasTag(TEXT("AllowDamage")))
            {
                UGameplayStatics::ApplyPointDamage(HitActor, AppliedDamage, ShotDir, FakeHit, GetOwner()->GetInstigatorController(), GetOwner(), UDamageType::StaticClass());
            }
            else if (HitActor->ActorHasTag(TEXT("AllowFeedback")))
            {
                UGameplayStatics::ApplyPointDamage(HitActor, 0.0f, ShotDir, FakeHit, GetOwner()->GetInstigatorController(), GetOwner(), UDamageType::StaticClass());
            }
        }
    }

    // 【核心优化】：如果射速小于 5.0 (单发/慢速武器)，才走精准的 Multicast 多播；
    // 否则直接跳过，把表现层交给客户端的 bIsFiring_Net 状态机去本地脑补！
    if (CurrentStats.FireRate < 5.0f)
    {
        // 伤害裁决完毕，向全网广播 3P 射击表现指令
        Multicast_PlayFireEffects(TargetLocations, TargetNormals, HitTypes);
    }
}

/*
 * Multicast_PlayFireEffects
 * - 在所有客户端播放 3P 的开火表现（武器实体假弹、枪口特效、3P 动画）
 * - 本地拥有者跳过以避免重复叠加本地已经播放的 1P 表现
 */
void UVRWeaponComponent::Multicast_PlayFireEffects_Implementation(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes)
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    // 优化：开枪者自己已经在本地跑过 1P 的视觉表现，直接跳过以节省性能，防止重叠双杀
    if (OwnerPawn && OwnerPawn->IsLocallyControlled()) return;

    // 远端呈现：触发武器实体的假子弹生成与枪口特效 (永远走 3P)
    if (EquippedWeapon && TargetLocations.Num() > 0)
    {
        EquippedWeapon->FireVisuals(TargetLocations, TargetNormals, HitTypes, false, CurrentStats.WeaponType);
    }

    // 远端呈现：播放 3P 人物全身开火蒙太奇
    if (CurrentStats.ThirdPersonFireMontage && CachedMesh3P)
    {
        if (UAnimInstance* AnimInst3P = CachedMesh3P->GetAnimInstance())
        {
            AnimInst3P->Montage_Play(CurrentStats.ThirdPersonFireMontage);
        }
    }
}

#pragma endregion


#pragma region /* 表现层回调 */

/**
 * OnRep_CurrentStats
 * - 当 CurrentStats（武器数值）从服务器复制到客户端时触发，用于更新本地 UI（准星类型）
 */
void UVRWeaponComponent::OnRep_CurrentStats()
{
    UpdateLocalCrosshair();
}

/**
 * UpdateLocalCrosshair
 * - 仅对本地玩家生效，通知 LocalVRAimActor 切换准星样式
 */
void UVRWeaponComponent::UpdateLocalCrosshair()
{
    if (CachedCharacter && CachedCharacter->IsLocallyControlled() && CachedCharacter->LocalVRAimActor)
    {
        CachedCharacter->LocalVRAimActor->UpdateCrosshairType(CurrentStats.WeaponType);
    }
}

#pragma endregion

#pragma region /* 性能优化：远端持续开火状态同步 */

/*
 * Server_SetFiringState
 * - 客户端上报是否处于持续开火（按住开火键）的状态到服务器
 * - 验证总是通过（可扩展）
 * - 服务器设置 bIsFiring_Net 并在 Listen Server 场景下手动触发 OnRep_IsFiringNet 以在主机上看到远端效果
 */
bool UVRWeaponComponent::Server_SetFiringState_Validate(bool bFiring) { return true; }

void UVRWeaponComponent::Server_SetFiringState_Implementation(bool bFiring)
{
    bIsFiring_Net = bFiring;

    // 【Listen Server 适配】：如果当前主机就是房主（非专用服务器），
    // 房主也需要看到其他客户端的 3P 表现，因此手动触发一次脑补逻辑。
    if (GetNetMode() != NM_DedicatedServer)
    {
        APawn* OwnerPawn = Cast<APawn>(GetOwner());
        if (OwnerPawn && !OwnerPawn->IsLocallyControlled())
        {
            OnRep_IsFiringNet();
        }
    }
}

/*
 * OnRep_IsFiringNet
 * - RepNotify：当 bIsFiring_Net 在远端发生改变时触发
 * - 收到 true 时：针对高射速武器启用脑补定时器
 * - 收到 false 时：无条件清理定时器以避免僵尸定时器
 */
void UVRWeaponComponent::OnRep_IsFiringNet()
{
    if (bIsFiring_Net)
    {
        // 收到开火状态：仅对高射速武器启用脑补定时器
        if (CurrentStats.FireRate >= 5.0f)
        {
            float TimeBetweenShots = 1.0f / CurrentStats.FireRate;
            GetWorld()->GetTimerManager().SetTimer(AutoFireVisualTimerHandle, this, &UVRWeaponComponent::PlayAutoFireVisuals, TimeBetweenShots, true, 0.0f);
        }
    }
    else
    {
        // 【关键修复】：收到停火状态时，无条件清理定时器！
        // 绝不能放在 FireRate >= 5.0f 的判断里，防止换成慢速枪后定时器变成孤儿。
        GetWorld()->GetTimerManager().ClearTimer(AutoFireVisualTimerHandle);
    }
}

/*
 * PlayAutoFireVisuals
 * - 远端脑补：在非拥有者客户端用定时器周期性产生 3P 表现（射线用于视觉判定）
 * - 此函数只负责表现，不做伤害裁决（服务器才做）
 */
void UVRWeaponComponent::PlayAutoFireVisuals()
{
    if (!EquippedWeapon) return;

    // 远端脑补：永远走 3P 的枪口插槽
    USphereComponent* ActiveSpawn = EquippedWeapon->ProjectileSpawn_3P;

    // 1. 获取枪口位置和方向
    FVector StartLoc = ActiveSpawn ? ActiveSpawn->GetComponentLocation() : GetOwner()->GetActorLocation();
    FVector ShootDir = ActiveSpawn ? ActiveSpawn->GetForwardVector() : GetOwner()->GetActorForwardVector();
    FVector UpDir = ActiveSpawn ? ActiveSpawn->GetUpVector() : GetOwner()->GetActorUpVector();

    float Range = CurrentStats.FireRange > 0.0f ? CurrentStats.FireRange : 10000.f;

    TArray<FVector> TargetLocations;
    TArray<FVector> TargetNormals;
    TArray<EVRHitType> HitTypes;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    FCollisionObjectQueryParams ObjectQueryParams;
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_ENEMY);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_WOOD);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_PROP);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_ROCK);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_FLOOR);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_WATER);
    ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjectQueryParams.AddObjectTypesToQuery(COLLISION_METAL);

    int32 TraceCount = FMath::Max(1, CurrentStats.TraceCount);

    if (CurrentStats.WeaponType == EVRWeaponType::HorizontalLine && TraceCount % 2 == 0)
    {
        TraceCount += 1;
    }

    float AngleGap = CurrentStats.HorizontalSpreadGap;
    float TotalAngle = (TraceCount - 1) * AngleGap;
    float StartAngle = -TotalAngle / 2.0f;

    for (int32 i = 0; i < TraceCount; i++)
    {
        FVector CurrentShootDir = ShootDir;

        if (CurrentStats.WeaponType == EVRWeaponType::HorizontalLine)
        {
            float CurrentAngle = StartAngle + (i * AngleGap);
            CurrentShootDir = ShootDir.RotateAngleAxis(CurrentAngle, UpDir);
        }
        else
        {
            CurrentShootDir = FMath::VRandCone(ShootDir, FMath::DegreesToRadians(CurrentStats.SpreadAngle + 2.0f));
        }

        FVector EndLoc = StartLoc + (CurrentShootDir * Range);
        FHitResult VisualHit;

        if (GetWorld()->LineTraceSingleByObjectType(VisualHit, StartLoc, EndLoc, ObjectQueryParams, Params))
        {
            TargetLocations.Add(VisualHit.ImpactPoint);
            TargetNormals.Add(VisualHit.ImpactNormal);
            HitTypes.Add(GetHitType(VisualHit.GetActor()));
        }
        else
        {
            TargetLocations.Add(EndLoc);
            TargetNormals.Add(-CurrentShootDir);
            HitTypes.Add(EVRHitType::None);
        }
    }

    // ==========================================
    // 3. 触发表现层 (强制传入 false 走 3P 表现)
    // ==========================================
    EquippedWeapon->FireVisuals(TargetLocations, TargetNormals, HitTypes, false, CurrentStats.WeaponType);

    // 4. 播放动画
    if (CurrentStats.ThirdPersonFireMontage && CachedMesh3P)
    {
        if (UAnimInstance* AnimInst3P = CachedMesh3P->GetAnimInstance())
        {
            AnimInst3P->Montage_Play(CurrentStats.ThirdPersonFireMontage);
        }
    }
}
#pragma endregion
