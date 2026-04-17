// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGun/VRAnimInstance_Gun/VRAnimInstance_Gun.h"
#include "VRAnimInstance_Arcade.generated.h"

/**
 * 机台模式专属动画实例
 * 负责提取头显旋转给脖子使用，并提取实体枪管上的插槽位置供双手 IK 使用。
 */
UCLASS()
class UE4HUNTING_API UVRAnimInstance_Arcade : public UVRAnimInstance_Gun
{
	GENERATED_BODY()
	
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // ==========================================
    // 动画蓝图驱动变量 (供蓝图读取)
    // ==========================================

    /** 给脖子/头部旋转用的独立偏航角和俯仰角 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|IK")
    FRotator HeadLookAtRotator;

    /** 左手 IK 目标的世界坐标变换 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|IK")
    FTransform LeftHandIKTransform;

    /** 右手 IK 目标的世界坐标变换 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|IK")
    FTransform RightHandIKTransform;
};
