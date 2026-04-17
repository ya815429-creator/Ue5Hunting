// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRGun/Global/Enum.h" 
#include "VRAimActor.generated.h"

class UWidgetComponent;

/**
 * VR 3D 准星实体载体
 * 作为一个纯本地视觉 Actor 生成在摄像机前方，利用 WidgetComponent 承载 2D 的 UVRAimWidget，实现深度契合的 VR 瞄准体验。
 */
UCLASS()
class UE4HUNTING_API AVRAimActor : public AActor
{
	GENERATED_BODY()

#pragma region /* 初始化与组件 */
public:
	AVRAimActor();

	/** 承载 2D UMG 准星的 3D 控件组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UWidgetComponent* WidgetComp;
#pragma endregion


#pragma region /* 状态转发接口 */
public:
	/** 将开火状态向下转发给内部的 Widget 实例 */
	void SetFiringState(bool bFiring);

	/** 将武器形态切换指令向下转发给内部的 Widget 实例 */
	void UpdateCrosshairType(EVRWeaponType InWeaponType);

	/** 将脉冲动画触发指令向下转发给内部的 Widget 实例 */
	void PlayFirePulse(float FireRate);
#pragma endregion
};