#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRGun/Global/VRStruct.h"
#include "VisualProjectile.generated.h"

/**
 * 纯视觉假子弹 Actor
 * 彻底抛弃了虚幻引擎底层的物理碰撞与扫掠计算，完全由武器组件注入飞行参数，
 * 利用定时器实现绝对精准的到达与视觉特效生成，极大降低高频开火时的服务器与本地性能开销。
 */
UCLASS()
class UE4HUNTING_API AVisualProjectile : public AActor
{
    GENERATED_BODY()

public:
    AVisualProjectile();

protected:
    virtual void BeginPlay() override;

#pragma region /* 核心配置与状态 */
public:
    /**
     * 隐形弹标记
     * 开启后该子弹实体仅作为计算延时爆炸特效的容器，不渲染任何模型网格体。
     * (常用于优化多射线并排发射时的视野遮挡问题)
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Projectile|Settings")
    bool bIsInvisible = false;
#pragma endregion


#pragma region /* 视觉与物理组件 */
public:
    /** 根碰撞组件 (已强制关闭物理阻挡，仅作为位移更新的根节点载体) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components")
    class USphereComponent* CollisionComponent;

    /** 假子弹视觉网格体 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components")
    class UStaticMeshComponent* StaticMesh;

    /** 弹道运动组件 (关闭了防穿模扫掠检测，仅提供匀速直线运动与姿态追踪) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components")
    class UProjectileMovementComponent* Projectile;
#pragma endregion


#pragma region /* 弹道参数缓存 */
public:
    /** 飞行初速度 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Settings")
    double Projectile_Speed;

    /** 弹道重力缩放比例 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Settings")
    double Projectile_Gravity_Scale;

    /** 实体存活的最大生命周期兜底值 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Settings")
    double Projectile_LifeSpan;

    /** 命中各类物理材质对应的粒子特效包 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Settings")
    FProjectileVFXs HitVFXs;
#pragma endregion


#pragma region /* 目标命中数据缓存 */
public:
    /** 由武器组件预先计算并直接注入的绝对爆炸终点坐标 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|HitData")
    FVector ExactTargetLocation;

    /** 命中表面的法线向量，用于校准受击特效的喷射方向 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|HitData")
    FVector ExactHitNormal;

    /** 命中目标的物理材质抽象枚举 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|HitData")
    EVRHitType HitMaterialType;
#pragma endregion


#pragma region /* 核心执行逻辑 */
protected:
    /** 控制精准爆炸的定时器句柄 */
    FTimerHandle ExplodeTimerHandle;

    /** 定时器回调：到达目标时间后触发受击特效并销毁自身 */
    UFUNCTION()
    void Explode();

public:
    /** 缓存从武器传过来的击中音效 */
    UPROPERTY()
    class USoundBase* ImpactSound;
    /**
     * 接收来自武器发射源的完整参数注入
     * 必须在 SpawnActorDeferred 与 FinishSpawningActor 之间被调用。
     */
    void InitFromWeapon(double InSpeed, double InGravity, double InLifeSpan, const FProjectileVFXs& InHitVFXs, FVector InTargetLoc, FVector InHitNormal, EVRHitType InHitType, USoundBase* InImpactSound = nullptr);
#pragma endregion
};