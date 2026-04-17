#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRGun/Global/VRStruct.h"
#include "Components/TimelineComponent.h"
#include "VRWeaponActor.generated.h"

/**
 * 武器视觉表现实体 Actor
 * 这是一个纯本地生成的表现层容器，负责承载 1P 和 3P 视角的武器模型、枪口火光以及动态材质表现。
 * 注意：由于它是双端独立生成的，不参与网络同步 (bReplicates = false)。
 */
UCLASS()
class UE4HUNTING_API AVRWeaponActor : public AActor
{
    GENERATED_BODY()

public:
    AVRWeaponActor();
    virtual void Tick(float DeltaTime) override;

protected:
    virtual void BeginPlay() override;

#pragma region /* 视觉组件 (1P & 3P 隔离) */
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
    class USphereComponent* RootCol;

    // ==========================================
    // 1P 视觉组件 (仅本地拥有者可见)
    // ==========================================
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|1P")
    class UStaticMeshComponent* WeaponMesh_1P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|1P")
    class USphereComponent* MuzzleFlashLoc_1P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|1P")
    class USphereComponent* ProjectileSpawn_1P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|1P")
    class UPointLightComponent* MuzzleLight_1P;

    // ==========================================
    // 3P 视觉组件 (仅远端其他玩家可见)
    // ==========================================
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|3P")
    class UStaticMeshComponent* WeaponMesh_3P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|3P")
    class USphereComponent* MuzzleFlashLoc_3P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|3P")
    class USphereComponent* ProjectileSpawn_3P;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components|3P")
    class UPointLightComponent* MuzzleLight_3P;
#pragma endregion


#pragma region /* 武器特效配置 */
public:
    /** 枪口火花粒子系统 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    class UNiagaraSystem* Projectile_Particle_MuzzleFlash;

    /** 连锁闪电枪专用的光束特效资产 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    class UNiagaraSystem* ChainLightningVFX;

    /** 枪口动态光源初始最大强度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    double Muzzle_Light_Intensity = 400.0;

    /** 枪口动态光源颜色 (默认偏暖黄) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    FLinearColor Muzzle_Light_Color = FLinearColor(1.0f, 0.8f, 0.2f, 1.0f);
#pragma endregion


#pragma region /* 假子弹弹道配置 */
public:
    /** 视觉假子弹 Actor 类 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Projectile")
    TSubclassOf<class AVisualProjectile> Projectile_Class;

    /** 假子弹飞行参数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Projectile")
    double Projectile_Speed = 8000.0;

    /*子弹重力*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Projectile")
    double Projectile_Gravity_Scale = 0.0;

    /*子弹生命周期*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Projectile")
    double Projectile_LifeSpan = 3.0;

    /** 命中不同材质时生成的对应特效配置 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Projectile")
    FProjectileVFXs Projectile_HitVFXs;
#pragma endregion


#pragma region /* 动态光源时间轴 */
protected:
    FTimeline LightTimeline;

    /** 控制开枪时枪口光照强度衰减的曲线 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    class UCurveFloat* LightCurve;

    UFUNCTION()
    void HandleLightProgress(float Value);

    UFUNCTION()
    void OnLightTimelineFinished();
#pragma endregion


#pragma region /* 武器登场特效时间轴 */
protected:
    FTimeline EquipTimeline;

    /** * 控制武器切换/生成时，材质参数 (Amount) 从 1 降到 0 的曲线。
     * 通常配合材质中的溶解或像素化浮现节点使用。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Effects")
    class UCurveFloat* EquipCurve;

    UFUNCTION()
    void HandleEquipProgress(float Value);
#pragma endregion


#pragma region /* 核心暴露接口 */
public:
    /** * 触发开火的各类视觉表现 (包括枪口特效、灯光、实体子弹及闪电光束生成)
     * @param TargetLocations 此次开火计算出的所有终点坐标
     * @param TargetNormals 此次开火命中表面的法线
     * @param HitTypes 各个落点的材质分类
     * @param bIsLocalFire 决定是由 1P 枪口还是 3P 枪口来激发表现
     * @param WeaponType 决定启用哪一种特殊视觉逻辑 (如闪电链)
     */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Action")
    virtual void FireVisuals(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, bool bIsLocalFire, EVRWeaponType WeaponType = EVRWeaponType::Normal);
#pragma endregion
};