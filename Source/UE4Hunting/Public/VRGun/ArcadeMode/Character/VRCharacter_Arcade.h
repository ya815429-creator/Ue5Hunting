#pragma once

#include "CoreMinimal.h"
#include "VRGun/VRCharacter_Gun.h"
#include "VRCharacter_Arcade.generated.h"

/**
 * 街机/机台专属角色类 (终极重构版)
 * 核心特性：无1P/3P隔离，独立挂载点，硬件电位器平滑驱动，全网精准同步。
 */
UCLASS()
class UE4HUNTING_API AVRCharacter_Arcade : public AVRCharacter_Gun
{
    GENERATED_BODY()

public:
    AVRCharacter_Arcade();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region /* 1. 核心组件与视觉配置 */
public:
    /** 机台武器的绝对物理固定挂载点 (跟随身体平移，不随头转) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Arcade|Component")
    class USceneComponent* WeaponMountPoint;

protected:
    /** 头部骨骼名称 (用于本地视角隐藏防遮挡) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Arcade|Visual")
    FName HeadBoneName = TEXT("head");
#pragma endregion

#pragma region /* 2. 硬件外设控制与物理死角限制 */
public:
    /**
     * 【蓝图极简接口】接收硬件电位器或鼠标的角度数据，驱动武器旋转。
     * 内部已实现本地权限隔离，蓝图直接调用即可，无需做任何 Branch 校验。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Arcade|Hardware")
    void UpdateArcadeWeaponRotation(float InPitch, float InYaw);

protected:
    // 旋转死角限制 (防止 IK 手臂扭曲穿模)
    UPROPERTY(EditAnywhere, Category = "VR|Arcade|Limits") float MinPitch = -30.0f;
    UPROPERTY(EditAnywhere, Category = "VR|Arcade|Limits") float MaxPitch = 45.0f;
    UPROPERTY(EditAnywhere, Category = "VR|Arcade|Limits") float MinYaw = -60.0f;
    UPROPERTY(EditAnywhere, Category = "VR|Arcade|Limits") float MaxYaw = 60.0f;

    // 平滑插值参数与状态
    UPROPERTY(EditAnywhere, Category = "VR|Arcade|Hardware") float WeaponRotationInterpSpeed = 15.0f;
    FRotator TargetWeaponRotation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Arcade|Hardware")
    FRotator CurrentWeaponRotation;
#pragma endregion

#pragma region /* 3. 网络同步核心架构 */
protected:
    /** 同步给全网的目标旋转角 */
    UPROPERTY(ReplicatedUsing = OnRep_ArcadeWeaponRotation)
    FRotator Rep_ArcadeWeaponRotation;

    UFUNCTION()
    void OnRep_ArcadeWeaponRotation();

    /** 客户端向服务器汇报最新角度 */
    UFUNCTION(Server, Unreliable, WithValidation)
    void Server_SetArcadeWeaponRotation(FRotator NewRot);

    // 发送频率控制 (防止摇杆高频数据引发网络风暴)
    float LastWeaponSyncTime = 0.0f;
    const float SYNC_INTERVAL = 0.05f; // 1秒同步20次
#pragma endregion
};