// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "VRWeaponComponent_Arcade.generated.h"

/**
 * 街机/机台专属武器组件
 * 重写了武器的吸附逻辑，不再跟随手部或摄像机，而是死死钉在机台挂载点上。
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UE4HUNTING_API UVRWeaponComponent_Arcade : public UVRWeaponComponent
{
	GENERATED_BODY()

public:
	// 重写挂载逻辑
	virtual void BindWeaponMeshesToCharacter(FName SocketName1P = NAME_None, FName SocketName3P = NAME_None) override;
protected:
	virtual void GetShootOriginAndDirection(FVector& OutStartLoc, FVector& OutForwardDir, FVector& OutUpDir) override;
		
};
