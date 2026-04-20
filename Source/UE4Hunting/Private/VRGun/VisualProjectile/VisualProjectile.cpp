#include "VRGun/VisualProjectile/VisualProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#pragma region /* 生命周期与初始化 */

/**
 * 构造函数：构建纯视觉弹道 Actor。
 * 核心优化在于彻底关闭了物理交互，以适应高射速或弹片极多时的性能需求。
 */
AVisualProjectile::AVisualProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    // 纯表现层 Actor，由本地武器组件独立生成，严禁网络同步
    bReplicates = false;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
    RootComponent = CollisionComponent;

    // 【核心优化】：纯视觉组件，彻底关闭碰撞，杜绝引擎底层的物理计算消耗和意外阻挡
    CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
    StaticMesh->SetupAttachment(RootComponent);
    StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Projectile = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile"));
    Projectile->UpdatedComponent = CollisionComponent;
    Projectile->bRotationFollowsVelocity = true; // 飞行姿态随速度方向自动修正
    Projectile->bSweepCollision = false;         // 严禁物理扫掠检测
}

void AVisualProjectile::BeginPlay()
{
    Super::BeginPlay();

    // 隐形弹机制（如用于优化水平射线枪的视觉遮挡），直接关闭渲染
    if (bIsInvisible)
    {
        SetActorHiddenInGame(true);
    }

    if (Projectile)
    {
        Projectile->InitialSpeed = Projectile_Speed;
        Projectile->MaxSpeed = Projectile_Speed;
        Projectile->ProjectileGravityScale = Projectile_Gravity_Scale;
    }
    SetLifeSpan(Projectile_LifeSpan);

    // 【逻辑接管】：因为没有物理碰撞阻挡，必须利用目标点计算飞行时间，使用定时器“准时”触发爆炸效果
    float Distance = FVector::Distance(GetActorLocation(), ExactTargetLocation);
    float TravelTime = Distance / (Projectile_Speed > 10.0f ? Projectile_Speed : 8000.0f);

    if (TravelTime <= 0.02f)
    {
        // 距离过近（例如贴脸开枪），直接引爆
        Explode();
    }
    else
    {
        GetWorld()->GetTimerManager().SetTimer(ExplodeTimerHandle, this, &AVisualProjectile::Explode, TravelTime, false);
    }
}

#pragma endregion


#pragma region /* 核心注入与表现触发 */

/**
 * 接收来自武器组件发射前注入的环境与目标参数
 */
void AVisualProjectile::InitFromWeapon(double InSpeed, double InGravity, double InLifeSpan, const FProjectileVFXs& InHitVFXs, FVector InTargetLoc, FVector InHitNormal, EVRHitType InHitType, USoundBase* InImpactSound)
{
    Projectile_Speed = InSpeed;
    Projectile_Gravity_Scale = InGravity;
    Projectile_LifeSpan = InLifeSpan;
    HitVFXs = InHitVFXs;

    ExactTargetLocation = InTargetLoc;
    ExactHitNormal = InHitNormal;
    HitMaterialType = InHitType;
    ImpactSound = InImpactSound;
}

/**
 * 到达目标时间后的爆炸表现处理
 */
void AVisualProjectile::Explode()
{
    // 隐藏 Mesh，防止在销毁帧出现残影
    if (StaticMesh) StaticMesh->SetVisibility(false, false);

    // 根据目标材质类型匹配对应的受击特效
    UNiagaraSystem* SelectedVFX = HitVFXs.DefaultHit;
    switch (HitMaterialType)
    {
    case EVRHitType::Enemy: SelectedVFX = HitVFXs.Enemy; break;
    case EVRHitType::Floor: SelectedVFX = HitVFXs.Floor; break;
    case EVRHitType::Water: SelectedVFX = HitVFXs.Water; break;
    case EVRHitType::Rock:  SelectedVFX = HitVFXs.Rock; break;
    case EVRHitType::Wood:  SelectedVFX = HitVFXs.Wood; break;
    case EVRHitType::QTE:   SelectedVFX = HitVFXs.QTE; break;
    case EVRHitType::Dirt:  SelectedVFX = HitVFXs.Dirt; break;
    case EVRHitType::Metal: SelectedVFX = HitVFXs.Metal; break;
    default: break;
    }

    // 生成受击粒子
    if (SelectedVFX)
    {
        // 构建旋转矩阵，确保特效总是沿着命中表面法线的方向散开
        FRotator SpawnRotation = UKismetMathLibrary::MakeRotFromX(ExactHitNormal);
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            GetWorld(), SelectedVFX, ExactTargetLocation, SpawnRotation,
            FVector(1.0f), true, true, ENCPoolMethod::None, true
        );
    }
    if (ImpactSound)
    {
        // 判断这颗子弹的主人是不是本地玩家
        bool bIsLocal = false;
        if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
        {
            bIsLocal = OwnerPawn->IsLocallyControlled();
        }

        if (bIsLocal)
        {
            // 【1P 本地击中】：自己打中的，给你最清晰的 2D 贴耳正反馈！音量 1.0
            UGameplayStatics::PlaySound2D(this, ImpactSound, 1.0f);
        }
        else
        {
            // 【3P 远端击中】：队友打中的，或者导播在看全景。
            // 同样用 2D 音效无视距离，但强行把音量压低到 0.2，只作为微弱的战场环境音，绝不喧宾夺主！
            UGameplayStatics::PlaySound2D(this, ImpactSound, 0.2f);
        }
    }
    Destroy();
}

#pragma endregion