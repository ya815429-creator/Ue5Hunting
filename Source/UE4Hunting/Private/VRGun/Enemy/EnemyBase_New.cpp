#include "VRGun/Enemy/EnemyBase_New.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "VRGun/Component/SequenceSyncComponent.h"
#include "Components/DecalComponent.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "TimerManager.h"
#include "VRGun/GameModeBase/VRGunGameModeBase.h"
#include "VRGun/QTE/QteBase_New.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"

// =====================================================
// EnemyBase_New.cpp
// 说明：
// 该文件实现了 AEnemyBase_New 的全部运行时逻辑，包括：
//  - 初始化（从数据表读取并配置 Mesh、碰撞、特效等）
//  - 伤害接收与死亡判定（服务器权威）
//  - 死亡表现分发（多播到客户端播放动画 / 布娃娃 / 特效）
//  - QTE（弱点）生成、管理与结算
//  - 小怪召唤功能
// =====================================================

#pragma region /* 生命周期与初始化 */

// 构造函数：创建默认子组件并设置复制、Tick 等基础属性
AEnemyBase_New::AEnemyBase_New()
{
    PrimaryActorTick.bCanEverTick = true; // 持续 Tick 用于时间轴等效果
    bReplicates = true; // 允许网络复制

    // 初始化子组件
    SyncComp = CreateDefaultSubobject<USequenceSyncComponent>(TEXT("SequenceSync"));
    PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));

    ExtraMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExtraMesh"));
    ExtraMesh->SetupAttachment(RootComponent);

    ShadowDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("ShadowDecal"));
    ShadowDecal->SetupAttachment(RootComponent);

    DirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
    DirectionArrow->SetupAttachment(RootComponent);

    bIsDead = false; // 初始未死亡
}

// 注册需要复制的属性：确保客户端能接收到服务器的配置与状态
void AEnemyBase_New::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEnemyBase_New, EnemyDataTable);
    DOREPLIFETIME(AEnemyBase_New, EnemyIndex);
    DOREPLIFETIME(AEnemyBase_New, EnemyMeshIndex);
    DOREPLIFETIME(AEnemyBase_New, bIsDead);
    DOREPLIFETIME(AEnemyBase_New, bIsAutoDead);
}

// BeginPlay：运行时初始化
// - 支持在编辑器中直接放置的预设实例（如果预设包含数据表索引则自动初始化）
// - 绑定受击闪光时间轴（仅当曲线资源存在时）
void AEnemyBase_New::BeginPlay()
{
    Super::BeginPlay();

    // 兜底初始化：若编辑器中直接放置了怪物且配置了数据表与索引，则尝试初始化
    if (EnemyDataTable && EnemyIndex != -1)
    {
        InitEnemyData(EnemyIndex, EnemyMeshIndex);
    }

    // 如果配置了受击闪光曲线，绑定时间轴的回调函数
    if (HitFlashCurve)
    {
        FOnTimelineFloat ProgressFunction;
        ProgressFunction.BindUFunction(this, FName("HandleHitFlashProgress"));
        HitFlashTimeline.AddInterpFloat(HitFlashCurve, ProgressFunction);
    }
}

// Tick：用于推进时间轴等效果（例如受击闪光）
void AEnemyBase_New::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    HitFlashTimeline.TickTimeline(DeltaTime);
}

// RepNotify：当 EnemyIndex / EnemyMeshIndex 在客户端复制完成后调用，确保客户端也能应用相应配置
void AEnemyBase_New::OnRep_EnemyData()
{
    if (EnemyDataTable && EnemyIndex != -1)
    {
        InitEnemyData(EnemyIndex, EnemyMeshIndex);
    }
}

// 从数据表加载并初始化敌人外观、碰撞、特效等信息
// 注意：仅读取资源与配置。本函数内对于运行时关键变量（如 Health）仅在 HasAuthority 时设置，以保证服务器权威
void AEnemyBase_New::InitEnemyData(int32 InEnemyIndex, int32 InEnemyMeshIndex)
{
    if (!EnemyDataTable) return;

    // 构造数据表行名并查询
    FName TargetRowName = *FString::Printf(TEXT("NewRow_%d"), InEnemyIndex);
    FEnemyEntityData* FoundRow = EnemyDataTable->FindRow<FEnemyEntityData>(TargetRowName, TEXT("InitEnemyData"));

    if (FoundRow)
    {
        EnemyRow = *FoundRow; // 复制配置到运行时缓存
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("无法在数据表中找到怪物配置行: %s"), *TargetRowName.ToString());
        return;
    }

    USkeletalMeshComponent* MyMesh = GetMesh();
    UCapsuleComponent* MyCapsule = GetCapsuleComponent();
    if (!MyMesh || !MyCapsule) return; // 必要组件缺失直接返回

    // 1) 根据选择的 MeshIndex 切换网格与变换
    if (EnemyRow.Mesh.IsValidIndex(InEnemyMeshIndex))
    {
        USkeletalMesh* TargetMesh = EnemyRow.Mesh[InEnemyMeshIndex];
        if (TargetMesh)
        {
            MyMesh->SetSkeletalMesh(TargetMesh);
            MyMesh->SetRelativeTransform(EnemyRow.MeshTranform);
            // 缓存基准位移/旋转用于平滑对齐（防止视觉拉扯）
            BaseTranslationOffset = EnemyRow.MeshTranform.GetLocation();
            BaseRotationOffset = EnemyRow.MeshTranform.GetRotation();
        }
    }

    // 2) 设置对应的动画蓝图类（如果配置了的话）
    if (EnemyRow.AbpClass.IsValidIndex(InEnemyMeshIndex))
    {
        if (TSubclassOf<UAnimInstance> TargetAbp = EnemyRow.AbpClass[InEnemyMeshIndex])
        {
            MyMesh->SetAnimInstanceClass(TargetAbp);
        }
    }

    // 3) 配置碰撞与附属静态网格
    MyCapsule->SetCapsuleSize(EnemyRow.CollisionCylinderSize.X, EnemyRow.CollisionCylinderSize.Y);
    if (ShadowDecal) ShadowDecal->SetRelativeTransform(EnemyRow.ShadowTranform);
    if (ExtraMesh && IsValid(EnemyRow.StaticMesh)) ExtraMesh->SetStaticMesh(EnemyRow.StaticMesh);

    // 4) 清理之前的 Niagara 组件，避免重复生成导致内存泄漏
    for (UNiagaraComponent* OldNia : DynamicNiagaraComps)
    {
        if (IsValid(OldNia)) OldNia->DestroyComponent();
    }
    DynamicNiagaraComps.Empty();

    // 5) 根据数据表配置生成新的 Niagara 特效组件并附着
    for (UNiagaraSystem* NiaSystem : EnemyRow.MeshNia)
    {
        if (IsValid(NiaSystem))
        {
            UNiagaraComponent* NewNiaComp = NewObject<UNiagaraComponent>(this);
            if (NewNiaComp)
            {
                NewNiaComp->RegisterComponent();
                NewNiaComp->AttachToComponent(MyCapsule, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
                NewNiaComp->SetAsset(NiaSystem);
                DynamicNiagaraComps.Add(NewNiaComp);
            }
        }
    }

    // 6) 仅在服务器端初始化运行时血量，确保服务器为权威
    if (HasAuthority() && !bHasInitialized)
    {
        Health = MaxHealth;
    }

    // 7) 如果该敌人使用物理死亡（布娃娃），将 PhysicalAnimation 绑定到 SkeletalMesh
    if (EnemyRow.DieType == EEnemyDeathType::Physics && PhysicalAnimation)
    {
        PhysicalAnimation->SetSkeletalMeshComponent(MyMesh);
    }

    bHasInitialized = true; // 标记已完成初始化，避免重复
}
#pragma endregion


#pragma region /* 战斗与伤害逻辑 */

// 伤害处理入口（覆盖 Actor::TakeDamage）
// 说明：
// - 该函数在服务器上对血量与死亡进行权威处理
// - 支持对 Boss 单独处理（Boss 不会按普通怪物死亡流程结算）
// - 支持对已死亡的尸体进行“鞭尸”判定（添加冲量等视觉反馈）
float AEnemyBase_New::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
    // 调用父类以获得框架级别的实际伤害数值
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    // 权限与零伤害过滤：只有服务器处理伤害逻辑，且伤害必须为正
    // 注意：这里故意不在死后直接 return，以便支持对已死尸体的鞭尸判定
    if (!HasAuthority() || ActualDamage <= 0.0f) return 0.0f;

    // 尝试从点伤害事件中提取射击方向与命中骨骼，供后续物理反馈使用
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        const FPointDamageEvent* PointDamageEvent = (FPointDamageEvent*)&DamageEvent;
        ServerLastShotDirection = PointDamageEvent->ShotDirection;
        ServerLastHitBone = PointDamageEvent->HitInfo.BoneName;
    }
    else
    {
        // 非点伤害的兜底方向
        ServerLastShotDirection = -GetActorForwardVector();
        ServerLastHitBone = NAME_None;
    }

    // Boss 专用处理：Boss 接受伤害但不会走普通小怪的死亡结算
    if (ActorHasTag(FName("Boss")))
    {
        // Boss 仅更新血量并触发 UI/表现，死亡结算另行处理（例如触发阶段切换）
        Health -= ActualDamage;
        if (Health < 0.0f) Health = 0.0f; // 防止负血

        ReceiveOnTakeDamage(Health, ActualDamage);
        PlayHitFlash();

        return ActualDamage;
    }

    // 如果已经死亡，执行“鞭尸”逻辑：对尸体添加冲量并播放受击闪光，但不再重复结算死亡
    if (bIsDead)
    {
        if (EnemyRow.DieType == EEnemyDeathType::Physics)
        {
            if (USkeletalMeshComponent* MyMesh = GetMesh())
            {
                // 计算基于上次射击方向的冲量向量并通过多播广播给所有客户端
                FVector ImpulseDirection = ServerLastShotDirection + FVector(0.0f, 0.0f, EnemyRow.Zmultiplier);
                ImpulseDirection.Normalize();

                FVector FinalImpulse = ImpulseDirection * EnemyRow.ImpulseMultiplier;
                Multicast_AddCorpseImpulse(FinalImpulse, ServerLastHitBone);
            }
            PlayHitFlash(); // 视觉反馈
        }

        // 已死亡的实体不再扣血或触发击杀结算
        return 0.0f;
    }

    // 正常存活状态下扣血与触发受伤表现
    Health -= ActualDamage;

    // 打印服务器日志以便调试（可在运行时查看）
    FString DmgLog = FString::Printf(TEXT("[Server] 怪物 %s 受到伤害: %.1f | 剩余血量: %.1f"), *GetName(), ActualDamage, Health);
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, DmgLog);
    UE_LOG(LogTemp, Warning, TEXT("%s"), *DmgLog);

    // 通知蓝图与播放受击闪光
    ReceiveOnTakeDamage(Health, ActualDamage);
    PlayHitFlash();

    // 如果血量耗尽，执行击杀结算与奖励发放（仅服务器）
    if (Health <= 0.0f)
    {
        Health = 0.0f;

        // 若配置了击杀奖励并且传入了施法者信息，发放奖励
        if (HasAuthority() && RewardType != EWeaponRewardType::None && EventInstigator)
        {
            if (APawn* InstigatorPawn = EventInstigator->GetPawn())
            {
                if (UVRWeaponComponent* WeaponComp = InstigatorPawn->FindComponentByClass<UVRWeaponComponent>())
                {
                    WeaponComp->GrantWeaponReward(RewardType);
                }
            }
        }
        Die(false); // 正常击杀（非剧情）
    }

    return ActualDamage;
}

// 自动死亡接口：通常由序列或关卡脚本调用（服务器调用）
void AEnemyBase_New::AutoDie()
{
    if (!HasAuthority() || bIsDead) return;

    Health = 0.0f;
    Die(true); // 标记为剧情/自动死亡
}

// 服务器级别的死亡核心逻辑：设置标识、清理绑定并触发全网死亡表现
void AEnemyBase_New::Die(bool bIsAutoDeath)
{
    if (bIsDead) return; // 幂等保护

    bIsDead = true;
    bIsAutoDead = bIsAutoDeath;

    // 记录日志，便于调试与回放
    FString DieLog = FString::Printf(TEXT("[Server] 怪物 %s 死亡! 是否为剧情杀: %s"), *GetName(), bIsAutoDeath ? TEXT("True") : TEXT("False"));
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, DieLog);
    UE_LOG(LogTemp, Warning, TEXT("%s"), *DieLog);

    // 禁用胶囊体碰撞，防止阻挡其他实体
    if (UCapsuleComponent* MyCapsule = GetCapsuleComponent())
    {
        MyCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // 解除与序列的绑定，防止序列继续驱动已死对象
    if (SyncComp) SyncComp->UnbindFromSequence();

    // 从标签集合中移除可伤害与敌人标记，避免被再次误判为目标
    Tags.Remove(FName("AllowDamage"));
    Tags.Remove(FName("Enemy"));

    // 若为剧情性死亡，直接在服务器上销毁，不进行多播表现
    if (bIsAutoDead)
    {
        Destroy();
        return;
    }

    // 通知所有客户端播放死亡表现（动画 / 物理 / 特效），由多播函数负责具体细节
    Multicast_PerformDeath(EnemyRow.DieType, bIsAutoDead, ServerLastShotDirection, ServerLastHitBone);
}

// 客户端 RepNotify 实现：客户端收到死亡状态后需要在本地触发蓝图事件与禁用碰撞
void AEnemyBase_New::OnRep_IsDead()
{
    if (bIsDead)
    {
        if (UCapsuleComponent* MyCapsule = GetCapsuleComponent())
        {
            MyCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        // 在客户端触发蓝图实现的死亡回调，供 UI / VFX 使用
        ReceiveOnDeath(bIsAutoDead);
    }
}
#pragma endregion


#pragma region /* 死亡表现分发 */

// 多播可靠实现：服务器调用，所有客户端（包含服务器）执行相同的视觉/物理死亡表现
void AEnemyBase_New::Multicast_PerformDeath_Implementation(EEnemyDeathType DeathType, bool bIsAuto, FVector ShotDirection, FName HitBoneName)
{
    // 1) 触发蓝图层的统一回调
    ReceiveOnDeath(bIsAuto);

    // 2) 根据配置类型选择具体表现
    switch (DeathType)
    {
    case EEnemyDeathType::Anim:
        AnimDie();
        break;
    case EEnemyDeathType::Physics:
        PhysicsDie(ShotDirection, HitBoneName);
        break;
    case EEnemyDeathType::Nia:
        NiaDie();
        break;
    default:
        break;
    }
}

// 动画死亡表现：适用于播放死亡蒙太奇后销毁的场景
void AEnemyBase_New::AnimDie()
{
    // 脱离父级以便自由控制，打开重力并切换碰撞配置
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    EnableGravity(true);
    SetDieCollisionEnabled();

    float AnimLength = 0.1f;
    if (IsValid(EnemyRow.DieAnim) && GetMesh() && GetMesh()->GetAnimInstance())
    {
        GetMesh()->GetAnimInstance()->Montage_Play(EnemyRow.DieAnim);
        AnimLength = EnemyRow.DieAnim->GetPlayLength();
    }

    // 仅服务器设置生存时间，避免客户端提前销毁产生错误
    if (HasAuthority())
    {
        SetLifeSpan(AnimLength);
    }
}

// 物理死亡表现：开启物理模拟并施加冲量，随后在服务器设置删除时间
void AEnemyBase_New::PhysicsDie(FVector ShotDirection, FName HitBoneName)
{
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    SetDieCollisionEnabled();
    EnableGravity(true);

    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        MyMesh->SetSimulatePhysics(true);

        FVector ImpulseDirection = ShotDirection + FVector(0.0f, 0.0f, EnemyRow.Zmultiplier);
        ImpulseDirection.Normalize();

        FVector FinalImpulse = ImpulseDirection * EnemyRow.ImpulseMultiplier;
        MyMesh->AddImpulse(FinalImpulse, HitBoneName, true);
    }

    // 服务器端设置尸体生命周期以及在短延迟后允许“鞭尸”标签
    if (HasAuthority())
    {
        SetLifeSpan(3.0f);

        FTimerHandle TagTimerHandle;
        GetWorldTimerManager().SetTimer(TagTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
            {
                Tags.AddUnique(FName("AllowDamage")); // 允许对尸体进行鞭尸
            }), 0.5f, false);
    }
}

// 特效死亡表现：隐藏 Mesh 并在指定位置生成 Niagara / 粒子特效，然后短时间后销毁
void AEnemyBase_New::NiaDie()
{
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (GetMesh()) GetMesh()->SetVisibility(false, true);
    if (ExtraMesh) ExtraMesh->SetVisibility(false, true);

    FVector SpawnLocation = FVector::ZeroVector;
    if (GetMesh() && GetMesh()->GetSkeletalMeshAsset()) SpawnLocation = GetMesh()->GetSocketLocation(EnemyRow.NiaSpawnBone);
    else if (DirectionArrow) SpawnLocation = DirectionArrow->GetComponentLocation();
    else SpawnLocation = GetActorLocation();

    if (IsValid(EnemyRow.DieNiagara))
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), EnemyRow.DieNiagara, SpawnLocation, FRotator::ZeroRotator, FVector(1.0f), true, true, ENCPoolMethod::None, true);
    }
    else if (IsValid(EnemyRow.DieParticles))
    {
        UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), EnemyRow.DieParticles, SpawnLocation, FRotator::ZeroRotator, FVector(1.0f), true, EPSCPoolMethod::None, true);
    }

    // 仅在服务器上真正销毁 Actor
    if (HasAuthority())
    {
        SetLifeSpan(0.2f);
    }
}

// 多播：为已经死亡的尸体添加冲量（不可靠即可，视觉差异可接受）
void AEnemyBase_New::Multicast_AddCorpseImpulse_Implementation(FVector Impulse, FName BoneName)
{
    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        if (MyMesh->IsSimulatingPhysics())
        {
            MyMesh->AddImpulse(Impulse, BoneName, true);
        }
    }
}
#pragma endregion


#pragma region /* 特效与视觉 */

// 播放受击闪光：服务器请求通过多播在所有客户端触发视觉效果
void AEnemyBase_New::PlayHitFlash()
{
    if (HasAuthority())
    {
        Multicast_PlayHitFlash();
    }
}

// 多播实现：在客户端启动受击闪光时间轴
void AEnemyBase_New::Multicast_PlayHitFlash_Implementation()
{
    if (HitFlashCurve)
    {
        HitFlashTimeline.PlayFromStart();
    }
}

// 时间轴采样：将当前曲线值写入材质参数（例如 HitAlpha）以驱动受击闪光
void AEnemyBase_New::HandleHitFlashProgress(float Value)
{
    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        MyMesh->SetScalarParameterValueOnMaterials(TEXT("HitAlpha"), Value);
    }
}
#pragma endregion


#pragma region /* QTE生成 */

// 启动 QTE 阶段：初始化状态、禁用碰撞并按配置开始分批生成
void AEnemyBase_New::StartQTEPhase(FEnemySpawnQte_VR InSpawn)
{
    // 1) 状态重置
    EnemySpawnQTE = InSpawn;
    CurrentSpawnNumber = 0;
    KilledQteNumber = 0;
    OwnedQte.Empty();

    // 2) 广播关闭自身 Mesh 碰撞，避免阻挡 QTE 交互
    Multicast_DisableMeshCollision();

    Tags.AddUnique(FName("AllowDamage"));

    // 3) 启动生成流（支持延迟生成与瞬时批量生成两种模式）
    double Delay = EnemySpawnQTE.DelaySpawnTime;

    UE_LOG(LogTemp, Warning, TEXT("[%s] StartQTEPhase: 启动 QTE 阶段！计划生成总数: %d, 延迟间隔: %f 秒"),
        *GetName(), EnemySpawnQTE.SpawnNumber, Delay);

    if (Delay > 0.0)
    {
        GetWorldTimerManager().SetTimer(SpawnQteTimerHandle, this, &AEnemyBase_New::SpawnNextQTE, (float)Delay, true, 0.0f);
    }
    else
    {
        for (int32 i = 0; i < EnemySpawnQTE.SpawnNumber; ++i)
        {
            SpawnNextQTE();
        }
    }
}

// 生成下一个 QTE 的回调（可由定时器驱动或瞬时批量调用）
void AEnemyBase_New::SpawnNextQTE()
{
    // 校验权限与参数边界，避免数据配置错误导致崩溃
    if (!HasAuthority() || !QteClassToSpawn || CurrentSpawnNumber >= EnemySpawnQTE.SpawnNumber || CurrentSpawnNumber >= EnemySpawnQTE.QteScale.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[%s] SpawnNextQTE 异常拦截终止！权限=%d, QTE类是否配置=%d, 当前生成数=%d, 缩放数组大小=%d"),
            *GetName(), HasAuthority(), QteClassToSpawn != nullptr, CurrentSpawnNumber, EnemySpawnQTE.QteScale.Num());

        GetWorldTimerManager().ClearTimer(SpawnQteTimerHandle);
        return;
    }

    // 根据当前索引获取缩放与生成变换
    double CurrentScale = EnemySpawnQTE.QteScale[CurrentSpawnNumber];
    FTransform SpawnTransform;
    SpawnTransform.SetScale3D(FVector(CurrentScale, CurrentScale, CurrentScale));

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // 生成 QTE Actor 并完成初始化
    AQteBase_New* SpawnedQTE = GetWorld()->SpawnActor<AQteBase_New>(QteClassToSpawn, SpawnTransform, SpawnParams);

    if (SpawnedQTE)
    {
        // 告诉 QTE 它的拥有者，方便 QTE 在死亡时回调 Owner
        SpawnedQTE->SetOwner(this);

        OwnedQte.Add(SpawnedQTE);

        // 如果配置了附着骨骼则附着到对应骨骼，并初始化 QTE 的运行时属性
        FName AttachBone = (CurrentSpawnNumber < EnemySpawnQTE.AttachBoneName.Num()) ? EnemySpawnQTE.AttachBoneName[CurrentSpawnNumber] : NAME_None;
        if (GetMesh() && AttachBone != NAME_None)
        {
            SpawnedQTE->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachBone);

            // 将配置数据写入 QTE 的 InitData，并调用服务器初始化接口
            SpawnedQTE->InitData.Duration = (CurrentSpawnNumber < EnemySpawnQTE.DurationTime.Num()) ? EnemySpawnQTE.DurationTime[CurrentSpawnNumber] : 5.0;
            SpawnedQTE->InitData.MaxHp = (CurrentSpawnNumber < EnemySpawnQTE.MaxHp.Num()) ? EnemySpawnQTE.MaxHp[CurrentSpawnNumber] : 100.0;
            SpawnedQTE->InitData.QTELevel = (CurrentSpawnNumber < EnemySpawnQTE.QTELevel.Num()) ? EnemySpawnQTE.QTELevel[CurrentSpawnNumber] : 1;
            SpawnedQTE->InitData.Scale = (float)CurrentScale;
            SpawnedQTE->Server_InitQTE();

            UE_LOG(LogTemp, Log, TEXT("[%s] SpawnNextQTE: 成功生成第 %d 个 QTE, 附着骨骼: %s, 寿命: %f, 血量: %f"),
                *GetName(), CurrentSpawnNumber + 1, *AttachBone.ToString(), SpawnedQTE->InitData.Duration, SpawnedQTE->InitData.MaxHp);
        }

        CurrentSpawnNumber++;

        // 当达到配置的生成数量时，停止定时器并广播生成完成事件
        if (CurrentSpawnNumber >= EnemySpawnQTE.SpawnNumber)
        {
            UE_LOG(LogTemp, Log, TEXT("[%s] SpawnNextQTE: 所有 %d 个 QTE 已经全部生成完毕，停止生成定时器。"), *GetName(), EnemySpawnQTE.SpawnNumber);
            GetWorldTimerManager().ClearTimer(SpawnQteTimerHandle);

            if (OnQteSpawnFinishedEvent.IsBound())
            {
                OnQteSpawnFinishedEvent.Broadcast();
            }
        }
    }
}

// QTE 自身在销毁时会调用 Owner->OnQteDestroyed 通知结果
void AEnemyBase_New::OnQteDestroyed(bool bKilledByPlayer, AQteBase_New* DestroyedQTE)
{
    // 1) 从 OwnedQte 列表移除已销毁实例
    OwnedQte.Remove(DestroyedQTE);

    // 2) 统计玩家击破数量（用于判定成功/失败）
    if (bKilledByPlayer)
    {
        KilledQteNumber++;
    }

    FString DieReason = bKilledByPlayer ? TEXT("被玩家击破") : TEXT("寿命耗尽自毁");
    UE_LOG(LogTemp, Log, TEXT("[%s] OnQteDestroyed: 收到 QTE 死亡汇报 (%s)！当前总击杀数: %d/%d, 场上剩余存活 QTE 数量: %d"),
        *GetName(), *DieReason, KilledQteNumber, EnemySpawnQTE.SpawnNumber, OwnedQte.Num());

    // 3) 若所有 QTE 已生成完毕且场上没有残留，立即进入结算
    if (CurrentSpawnNumber >= EnemySpawnQTE.SpawnNumber && OwnedQte.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] OnQteDestroyed: 场上所有 QTE 已清理完毕，准备进入终极结算环节..."), *GetName());
        ResolveQTEPhase();
    }
}

// 解析 QTE 阶段结果：判定玩家是否成功并根据配置对 Boss 施加后果（如伤害），最后广播阶段结束事件
void AEnemyBase_New::ResolveQTEPhase()
{
    // 1) 判定输赢逻辑
    bool bSuccess = (KilledQteNumber >= EnemySpawnQTE.SpawnNumber);
    bool bApplyDamage = EnemySpawnQTE.bIsApplyDamage || EnemySpawnQTE.bQteFailedIsApplyDamage;
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] ResolveQTEPhase: 🏆 QTE 挑战成功！玩家全清了 %d 个弱点。"), *GetName(), KilledQteNumber);
        // 成功的额外逻辑（如加分）已注释，未来可按需打开
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[%s] ResolveQTEPhase: 💀 QTE 挑战失败！要求击杀 %d 个，玩家仅击杀 %d 个。"),
            *GetName(), EnemySpawnQTE.SpawnNumber, KilledQteNumber);
    }

    // 2) 根据配置决定是否对 Boss 造成系统伤害
    if (bApplyDamage)
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] ResolveQTEPhase: 正在对怪物自身造成 %f 点系统伤害..."), *GetName(), EnemySpawnQTE.OwnerDamage);
        UGameplayStatics::ApplyDamage(
            this,
            EnemySpawnQTE.OwnerDamage,
            nullptr,
            nullptr,
            UDamageType::StaticClass()
        );
    }

    // 3) 广播阶段结束事件，外界可绑定以触发后续流程
    if (OnQtePhaseFinishedEvent.IsBound())
    {
        OnQtePhaseFinishedEvent.Broadcast(bSuccess);
    }
}
#pragma endregion


#pragma region /* 辅助工具 */

// 启用或禁用重力：同时作用于 Mesh、胶囊与角色运动组件
void AEnemyBase_New::EnableGravity(bool bEnable)
{
    if (GetMesh()) GetMesh()->SetEnableGravity(bEnable);
    if (GetCapsuleComponent()) GetCapsuleComponent()->SetEnableGravity(bEnable);
    if (GetCharacterMovement()) GetCharacterMovement()->GravityScale = bEnable ? 1.0f : 0.0f;
}

// 根据死亡类型切换到对应的死亡碰撞配置
void AEnemyBase_New::SetDieCollisionEnabled()
{
    switch (EnemyRow.DieType)
    {
    case EEnemyDeathType::Anim:
        SetDieCollisionEnabled_Anim();
        break;
    case EEnemyDeathType::Physics:
        SetDieCollisionEnabled_Physics();
        break;
    default:
        break;
    }
}

// 动画死亡时的碰撞与运动设置（例如先播放动画再禁用或改变碰撞）
void AEnemyBase_New::SetDieCollisionEnabled_Anim()
{
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement()) MoveComp->SetMovementMode(EMovementMode::MOVE_Falling);

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        Capsule->SetCollisionResponseToChannel(COLLISION_FLOOR, ECR_Block);
        Capsule->SetCollisionResponseToChannel(COLLISION_ENEMY, ECR_Overlap);
        Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        Capsule->SetCollisionObjectType(ECC_Pawn);
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComp->SetCollisionResponseToChannel(COLLISION_FLOOR, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_WOOD, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_ROCK, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_WATER, ECR_Block);

        // 如果配置了动画死亡的额外偏移，则添加相对位移以确保碰撞检测正确
        if (EnemyRow.IsOpenAnimDieCheck)
        {
            MeshComp->AddRelativeLocation(FVector(0.f, 0.f, EnemyRow.CollisionCylinderSize.X), false, nullptr, ETeleportType::None);
        }
    }
}

// 物理死亡（布娃娃）时的碰撞设置：启用物理碰撞并将对象标记为物理主体
void AEnemyBase_New::SetDieCollisionEnabled_Physics()
{
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetCollisionObjectType(ECC_PhysicsBody);
        MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        MeshComp->SetCollisionResponseToChannel(COLLISION_FLOOR, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_WOOD, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_ROCK, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(COLLISION_WATER, ECR_Block);
    }
}
void AEnemyBase_New::Multicast_DisableMeshCollision_Implementation()
{
    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}
#pragma endregion


#pragma region /* 召唤小怪系统实现 */

// 在战斗中召唤小怪的入口函数（仅服务器调用）
void AEnemyBase_New::SpawnMinions(TSubclassOf<class AEnemyBase_New> InMinionClass, FSpawnEnemyStruct InSpawnData, TArray<int32> InEnemyIndices, TArray<int32> InEnemyMeshIndices)
{
    // 权限与状态校验：仅服务器且自身未死亡时方可召唤
    if (!HasAuthority() || bIsDead) return;

    // 缓存传入数据以供回调使用
    MinionClassToSpawn = InMinionClass;
    ActiveMinionSpawnData = InSpawnData;
    ActiveMinionIndices = InEnemyIndices;
    ActiveMinionMeshIndices = InEnemyMeshIndices;

    // 严格校验数组长度，确保为每个要生成的小怪提供必要的参数
    if (ActiveMinionSpawnData.SpawnNumber > 0 &&
        ActiveMinionSpawnData.SequenceTags.Num() >= ActiveMinionSpawnData.SpawnNumber &&
        ActiveMinionSpawnData.MaxHp.Num() >= ActiveMinionSpawnData.SpawnNumber &&
        ActiveMinionIndices.Num() >= ActiveMinionSpawnData.SpawnNumber &&
        ActiveMinionMeshIndices.Num() >= ActiveMinionSpawnData.SpawnNumber)
    {
        CurrentMinionSpawnNumber = 0;

        // 若 DelayTime <= 0，立即一次性生成全部小怪
        if (ActiveMinionSpawnData.SpawnDelayTime <= 0.0f)
        {
            int32 TotalSpawns = ActiveMinionSpawnData.SpawnNumber;
            for (int32 i = 0; i < TotalSpawns; ++i)
            {
                SpawnMinionCallBack();
            }
        }
        else
        {
            // 否则启动定时器按间隔生成
            GetWorldTimerManager().SetTimer(
                TimerHandle_MinionSpawn,
                this,
                &AEnemyBase_New::SpawnMinionCallBack,
                ActiveMinionSpawnData.SpawnDelayTime,
                true,
                0.0f
            );
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[%s] 召唤小怪失败！参数数组长度不匹配，请检查传入的数组。"), *GetName());
    }
}

// 小怪生成的定时器回调：逐一注入参数、FinishSpawning 并绑定到本地序列（如有）
void AEnemyBase_New::SpawnMinionCallBack()
{
    // 保障：若 Boss 死亡或参数缺失，停止生成并清理定时器
    if (!HasAuthority() || !MinionClassToSpawn || bIsDead)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_MinionSpawn);
        return;
    }

    // 根据当前索引提取该小怪的配置
    FName TempSequenceTag = ActiveMinionSpawnData.SequenceTags[CurrentMinionSpawnNumber];
    float TempMaxHP = ActiveMinionSpawnData.MaxHp[CurrentMinionSpawnNumber];
    int32 TempEnemyIndex = ActiveMinionIndices[CurrentMinionSpawnNumber];
    int32 TempMeshIndex = ActiveMinionMeshIndices[CurrentMinionSpawnNumber];

    EWeaponRewardType TempReward = EWeaponRewardType::None;
    if (ActiveMinionSpawnData.RewardTypes.IsValidIndex(CurrentMinionSpawnNumber))
    {
        TempReward = ActiveMinionSpawnData.RewardTypes[CurrentMinionSpawnNumber];
    }

    // 延迟生成（SpawnActorDeferred）以便在 FinishSpawning 之前注入初始化参数
    AEnemyBase_New* SpawnedMinion = GetWorld()->SpawnActorDeferred<AEnemyBase_New>(
        MinionClassToSpawn,
        FTransform::Identity,
        this,    // 将 Boss 设置为 Owner
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    );

    if (SpawnedMinion)
    {
        // 注入战斗/生命周期参数
        SpawnedMinion->EnemyIndex = TempEnemyIndex;
        SpawnedMinion->EnemyMeshIndex = TempMeshIndex;
        SpawnedMinion->MaxHealth = TempMaxHP;
        SpawnedMinion->Health = TempMaxHP;
        SpawnedMinion->MyBindingTag = TempSequenceTag;
        SpawnedMinion->RewardType = TempReward;

        // 完成生成并触发 BeginPlay 等生命周期逻辑
        UGameplayStatics::FinishSpawningActor(SpawnedMinion, FTransform::Identity);

        // 将生成的小怪绑定到全局本地序列（若 GameState 提供了 LocalSequenceActor）
        if (SpawnedMinion->SyncComp)
        {
            AVRGunGameStateBase* GS = Cast<AVRGunGameStateBase>(GetWorld()->GetGameState());
            if (GS && GS->LocalSequenceActor)
            {
                SpawnedMinion->SyncComp->LocalInitAndBind(GS->LocalSequenceActor, GS->ActiveSequenceStartTime, TempSequenceTag);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[%s] 成功召唤小怪！Index: %d, Mesh: %d, Tag: %s"), *GetName(), TempEnemyIndex, TempMeshIndex, *TempSequenceTag.ToString());
    }

    CurrentMinionSpawnNumber++;

    // 达到期望数量时清理定时器
    if (CurrentMinionSpawnNumber >= ActiveMinionSpawnData.SpawnNumber)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_MinionSpawn);
    }
}

#pragma endregion
