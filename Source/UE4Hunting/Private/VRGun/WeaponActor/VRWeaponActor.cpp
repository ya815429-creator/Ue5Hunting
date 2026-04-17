#include "VRGun/WeaponActor/VRWeaponActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "VRGun/VisualProjectile/VisualProjectile.h"
#include "NiagaraComponent.h"
#include "VRGun/Pawn/DirectorPawn.h"
#include "EngineUtils.h"
#include "VRGun/VRCharacter_Gun.h"
#include "ProSceneCaptureComponent2D.h"
#pragma region /* 构造与初始化 */

/**
 * 构造函数：负责实例化纯视觉武器的所有组件，并严格隔离 1P（本地第一人称）与 3P（远端第三人称）的渲染层级。
 */
AVRWeaponActor::AVRWeaponActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 【架构说明】：此 Actor 为纯本地视觉表现容器，由组件（WeaponComponent）在各端独立生成。
    // 因此严禁开启网络同步，避免视觉双重生成或位置平滑拉扯。
    bReplicates = false;

    RootCol = CreateDefaultSubobject<USphereComponent>(TEXT("RootCol"));
    RootComponent = RootCol;

    // ==========================================
    // 1P 组件组装与隔离 (仅限本地玩家摄像机渲染)
    // ==========================================
    WeaponMesh_1P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh_1P"));
    WeaponMesh_1P->SetupAttachment(RootComponent);
    WeaponMesh_1P->SetOnlyOwnerSee(true);  // 仅自己可见
    WeaponMesh_1P->SetCastShadow(false);   // 防止 1P 武器投射巨大阴影穿帮

    MuzzleFlashLoc_1P = CreateDefaultSubobject<USphereComponent>(TEXT("MuzzleFlashLoc_1P"));
    MuzzleFlashLoc_1P->SetupAttachment(WeaponMesh_1P);

    ProjectileSpawn_1P = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileSpawn_1P"));
    ProjectileSpawn_1P->SetupAttachment(WeaponMesh_1P);

    MuzzleLight_1P = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight_1P"));
    MuzzleLight_1P->SetupAttachment(WeaponMesh_1P);

    // ==========================================
    // 3P 组件组装与隔离 (仅限远端其他玩家渲染)
    // ==========================================
    WeaponMesh_3P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh_3P"));
    WeaponMesh_3P->SetupAttachment(RootComponent);
    WeaponMesh_3P->SetOwnerNoSee(true);    // 自己不可见，防止遮挡视野

    MuzzleFlashLoc_3P = CreateDefaultSubobject<USphereComponent>(TEXT("MuzzleFlashLoc_3P"));
    MuzzleFlashLoc_3P->SetupAttachment(WeaponMesh_3P);

    ProjectileSpawn_3P = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileSpawn_3P"));
    ProjectileSpawn_3P->SetupAttachment(WeaponMesh_3P);

    MuzzleLight_3P = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight_3P"));
    MuzzleLight_3P->SetupAttachment(WeaponMesh_3P);
}

void AVRWeaponActor::BeginPlay()
{
    Super::BeginPlay();

    // 初始化枪口火光颜色
    if (MuzzleLight_1P) MuzzleLight_1P->SetLightColor(Muzzle_Light_Color);
    if (MuzzleLight_3P) MuzzleLight_3P->SetLightColor(Muzzle_Light_Color);

    // 绑定枪口火光渐变时间轴
    if (LightCurve)
    {
        FOnTimelineFloat ProgressFunction;
        ProgressFunction.BindUFunction(this, FName("HandleLightProgress"));
        LightTimeline.AddInterpFloat(LightCurve, ProgressFunction);

        FOnTimelineEvent FinishedFunction;
        FinishedFunction.BindUFunction(this, FName("OnLightTimelineFinished"));
        LightTimeline.SetTimelineFinishedFunc(FinishedFunction);
    }

    // 初始化并播放武器装备时的材质溶解/浮现特效时间轴
    if (EquipCurve)
    {
        FOnTimelineFloat EquipProgress;
        EquipProgress.BindUFunction(this, FName("HandleEquipProgress"));
        EquipTimeline.AddInterpFloat(EquipCurve, EquipProgress);

        // 无论是进游戏初始化的默认枪，还是中途吃道具换枪，都会触发此处，播放数值 1->0 的特效
        EquipTimeline.PlayFromStart();
    }
    else
    {
        // 防呆设计：如果未配置时间轴曲线，直接传 0 使武器立刻处于完整显示状态
        HandleEquipProgress(0.0f);
    }
}

void AVRWeaponActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    LightTimeline.TickTimeline(DeltaTime);
    EquipTimeline.TickTimeline(DeltaTime);
}

#pragma endregion


#pragma region /* 时间轴更新回调 */

/**
 * 枪口火光衰减插值回调
 */
void AVRWeaponActor::HandleLightProgress(float Value)
{
    float NewIntensity = UKismetMathLibrary::Lerp(Muzzle_Light_Intensity, 0.0, (double)Value);

    // 【性能优化】：同时更新两盏灯的亮度。
    // 隐藏的灯光即使数值改变也不会进入渲染管线计算，因此双重更新不造成额外负担。
    if (MuzzleLight_1P) MuzzleLight_1P->SetIntensity(NewIntensity);
    if (MuzzleLight_3P) MuzzleLight_3P->SetIntensity(NewIntensity);
}

/**
 * 枪口火光衰减结束回调
 */
void AVRWeaponActor::OnLightTimelineFinished()
{
    // 动画结束时彻底隐藏光源组件
    if (MuzzleLight_1P) MuzzleLight_1P->SetVisibility(false);
    if (MuzzleLight_3P) MuzzleLight_3P->SetVisibility(false);
}

/**
 * 武器材质溶解/显现参数更新回调
 */
void AVRWeaponActor::HandleEquipProgress(float Value)
{
    // 动态修改 1P 和 3P 材质实例上的 "Amount" 标量参数
    if (WeaponMesh_1P) WeaponMesh_1P->SetScalarParameterValueOnMaterials(TEXT("Amount"), Value);
    if (WeaponMesh_3P) WeaponMesh_3P->SetScalarParameterValueOnMaterials(TEXT("Amount"), Value);
}

#pragma endregion


#pragma region /* 开火视觉表现生成 */

/**
 * 开火表现统筹函数
 * 负责在本地渲染枪口火花、光照、子弹实体以及闪电链 Niagara 特效。
 */
void AVRWeaponActor::FireVisuals(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, bool bIsLocalFire, EVRWeaponType WeaponType)
{
    // ==========================================
     // 1. 激活枪口动态光源 (同步双端)
     // ==========================================
    if (LightCurve)
    {
        if (MuzzleLight_3P) MuzzleLight_3P->SetVisibility(true);
        if (bIsLocalFire && MuzzleLight_1P) MuzzleLight_1P->SetVisibility(true);
        LightTimeline.PlayFromStart();
    }

    // ==========================================
    // 2. 生成枪口火花 Niagara 粒子
    // ==========================================
    if (Projectile_Particle_MuzzleFlash)
    {
        // 【3P 特效轨】：永远生成（供导播和其它客户端观看）
        if (MuzzleFlashLoc_3P)
        {
            UNiagaraComponent* FX_3P = UNiagaraFunctionLibrary::SpawnSystemAttached(Projectile_Particle_MuzzleFlash, MuzzleFlashLoc_3P, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);

            // 🌟 权限屏蔽：如果是本地开火，主人(1P)闭眼不看，留给导播大屏看！
            if (FX_3P && bIsLocalFire)
            {
                FX_3P->SetOwnerNoSee(true);
            }
        }

        // 【1P 特效轨】：本地玩家专属
        if (bIsLocalFire && MuzzleFlashLoc_1P)
        {
            UNiagaraComponent* FX_1P = UNiagaraFunctionLibrary::SpawnSystemAttached(Projectile_Particle_MuzzleFlash, MuzzleFlashLoc_1P, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);

            // 🌟 权限屏蔽：这簇火花“仅主人可见”，导播绝对看不见！
            if (FX_1P)
            {
                FX_1P->SetOnlyOwnerSee(true);
            }
        }
    }

    // ==========================================
    // 3. 连锁闪电枪 (ChainGun) 专属视觉表现
    // ==========================================
    if (WeaponType == EVRWeaponType::ChainGun && ChainLightningVFX && TargetLocations.Num() > 0)
    {
        // 使用 Lambda 匿名函数复用生成逻辑
        auto SpawnLightningTrack = [&](USphereComponent* SpawnPoint, bool bIs1PTrack)
            {
                if (!SpawnPoint) return;
                FVector MuzzleLoc = SpawnPoint->GetComponentLocation();
                FVector FirstHitLoc = TargetLocations[0];

                // 生成主闪电
                UNiagaraComponent* MainBeam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    GetWorld(), ChainLightningVFX, MuzzleLoc, FRotator::ZeroRotator, FVector(1.0f), true, true, ENCPoolMethod::None, true);

                if (MainBeam)
                {
                    MainBeam->SetVariableVec3(FName("Location"), FirstHitLoc);
                    // 🌟 权限屏蔽
                    if (bIs1PTrack) MainBeam->SetOnlyOwnerSee(true);           // 1P轨：仅主人可见，导播瞎
                    else if (bIsLocalFire) MainBeam->SetOwnerNoSee(true);      // 3P轨：主人不看，留给导播看
                }

                // 生成分支跳跃闪电
                for (int32 i = 1; i < TargetLocations.Num(); i++)
                {
                    UNiagaraComponent* BranchBeam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                        GetWorld(), ChainLightningVFX, FirstHitLoc, FRotator::ZeroRotator, FVector(1.0f), true, true, ENCPoolMethod::None, true);

                    if (BranchBeam)
                    {
                        BranchBeam->SetVariableVec3(FName("Location"), TargetLocations[i]);
                        // 🌟 权限屏蔽
                        if (bIs1PTrack) BranchBeam->SetOnlyOwnerSee(true);     // 1P轨：仅主人可见，导播瞎
                        else if (bIsLocalFire) BranchBeam->SetOwnerNoSee(true);// 3P轨：主人不看，留给导播看
                    }
                }
            };

        // 执行双轨生成
        SpawnLightningTrack(ProjectileSpawn_3P, false);                  // 永远生成 3P 闪电轨
        if (bIsLocalFire) SpawnLightningTrack(ProjectileSpawn_1P, true); // 本地额外生成 1P 闪电轨
    }

    // ==========================================
    // 4. 生成实体弹道 (VisualProjectile) 
    // ==========================================
    if (Projectile_Class)
    {
        int32 CenterIndex = TargetLocations.Num() / 2;

        // 使用 Lambda 函数封装弹道实体的生成
        auto SpawnProjectileTrack = [&](USphereComponent* SpawnPoint, bool bIs1PTrack)
            {
                if (!SpawnPoint) return;
                FVector SpawnLoc = SpawnPoint->GetComponentLocation();

                for (int32 i = 0; i < TargetLocations.Num(); i++)
                {
                    if (WeaponType == EVRWeaponType::ChainGun && i > 0) continue;

                    FVector TargetLoc = TargetLocations[i];
                    FVector HitNormal = TargetNormals.IsValidIndex(i) ? TargetNormals[i] : FVector::UpVector;
                    EVRHitType HitType = HitTypes.IsValidIndex(i) ? HitTypes[i] : EVRHitType::None;

                    FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLoc);
                    FTransform SpawnTransform(LookAtRot, SpawnLoc, FVector(1.0f));

                    AVisualProjectile* VisProj = GetWorld()->SpawnActorDeferred<AVisualProjectile>(Projectile_Class, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

                    if (VisProj)
                    {
                        // 🌟 权限基石：必须把生成的实体弹道的 Owner 设置为玩家！
                        // 只有明确了“子弹的主人是谁”，后面的权限屏蔽才能生效！
                        VisProj->SetOwner(this->GetOwner());

                        VisProj->InitFromWeapon(Projectile_Speed, Projectile_Gravity_Scale, Projectile_LifeSpan, Projectile_HitVFXs, TargetLoc, HitNormal, HitType);

                        // 水平射线枪隐形弹处理
                        if (WeaponType == EVRWeaponType::HorizontalLine && i != CenterIndex)
                        {
                            VisProj->bIsInvisible = true;
                        }

                        UGameplayStatics::FinishSpawningActor(VisProj, SpawnTransform);

                        // 🌟 权限屏蔽：遍历子弹实体的所有渲染组件并打上标签
                        TArray<UPrimitiveComponent*> ProjComps;
                        VisProj->GetComponents<UPrimitiveComponent>(ProjComps);
                        for (UPrimitiveComponent* Comp : ProjComps)
                        {
                            if (bIs1PTrack)
                            {
                                Comp->SetOnlyOwnerSee(true);  // 1P轨：仅 1P 自己看，导播看不见
                            }
                            else if (bIsLocalFire)
                            {
                                Comp->SetOwnerNoSee(true);    // 3P轨且本地开火：1P 自己不看，留给导播看
                            }
                        }
                    }
                }
            };

        // 执行双轨生成
        SpawnProjectileTrack(ProjectileSpawn_3P, false);                  // 永远生成 3P 实体弹道轨
        if (bIsLocalFire) SpawnProjectileTrack(ProjectileSpawn_1P, true); // 本地额外生成 1P 实体弹道轨
    }
}

#pragma endregion