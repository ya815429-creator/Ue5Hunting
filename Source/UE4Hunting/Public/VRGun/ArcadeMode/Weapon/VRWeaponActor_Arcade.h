// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGun/WeaponActor/VRWeaponActor.h"
#include "VRWeaponActor_Arcade.generated.h"

UCLASS()
class UE4HUNTING_API AVRWeaponActor_Arcade : public AVRWeaponActor
{
	GENERATED_BODY()

	AVRWeaponActor_Arcade();
public:
	//重写视觉表现总控，强制拦截 1P 特效请求
	virtual void FireVisuals(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, bool bIsLocalFire, EVRWeaponType WeaponType = EVRWeaponType::Normal) override;
};
