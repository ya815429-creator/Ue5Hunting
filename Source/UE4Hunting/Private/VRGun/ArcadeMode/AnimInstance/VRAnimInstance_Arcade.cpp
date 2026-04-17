// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/ArcadeMode/AnimInstance/VRAnimInstance_Arcade.h"
#include "VRGun/ArcadeMode/Character/VRCharacter_Arcade.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "VRGun/WeaponActor/VRWeaponActor.h"

void UVRAnimInstance_Arcade::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!CachedCharacter) return;

    // ==========================================
    // 1. 头显旋转处理 (分离给脖子专用)
    // ==========================================
    // 基类已经算好了带平滑的 SmoothHeadTransform (相对旋转)
    FRotator HeadRot = SmoothHeadTransform.Rotator();
    // 提取 Pitch(抬头低头) 和 Yaw(左右转头)，Roll通常不应用给脖子防止骨骼扭曲畸形
    HeadLookAtRotator = FRotator(HeadRot.Pitch, HeadRot.Yaw, 0.0f);

    // ==========================================
    // 2. 实时获取机台枪械的双手把手插槽位置 (IK 目标)
    // ==========================================
    if (UVRWeaponComponent* WeaponComp = CachedCharacter->FindComponentByClass<UVRWeaponComponent>())
    {
        if (WeaponComp->EquippedWeapon && WeaponComp->EquippedWeapon->WeaponMesh_3P)
        {
            // 获取 3P 实体枪上的左右手把手插槽的世界坐标
            // (需要你在引擎里打开枪的模型，添加这两个 Socket)
            LeftHandIKTransform = WeaponComp->EquippedWeapon->WeaponMesh_3P->GetSocketTransform(TEXT("LeftHand_Grip"), RTS_World);
            RightHandIKTransform = WeaponComp->EquippedWeapon->WeaponMesh_3P->GetSocketTransform(TEXT("RightHand_Grip"), RTS_World);
        }
    }
}
