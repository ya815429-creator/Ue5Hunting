// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/ArcadeMode/Component/VRWeaponComponent_Arcade.h"
#include "VRGun/ArcadeMode/Character/VRCharacter_Arcade.h"
#include "VRGun/WeaponActor/VRWeaponActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
void UVRWeaponComponent_Arcade::BindWeaponMeshesToCharacter(FName SocketName1P, FName SocketName3P)
{
    // 调用时机和缓存防御保持一致
    if (!CachedCharacter)
    {
        CachedCharacter = Cast<AVRCharacter_Gun>(GetOwner());
    }

    if (!EquippedWeapon || !CachedCharacter) return;

    // 尝试转换为机台角色
    AVRCharacter_Arcade* ArcadeChar = Cast<AVRCharacter_Arcade>(CachedCharacter);
    if (ArcadeChar && ArcadeChar->WeaponMountPoint)
    {
        // 1. 彻底雪藏 1P 武器模型 (机台模式不需要两把枪重叠)
        if (EquippedWeapon->WeaponMesh_1P)
        {
            EquippedWeapon->WeaponMesh_1P->SetHiddenInGame(true);
            EquippedWeapon->WeaponMesh_1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        // 2. 改造 3P 武器模型作为唯一的“实体机台武器”
        if (EquippedWeapon->WeaponMesh_3P)
        {
            // 让本地玩家也能看到这把实体枪
            EquippedWeapon->WeaponMesh_3P->SetOwnerNoSee(false);

            // 【核心】：不再挂载到角色手部的 Socket，而是直接吸附到胸前死死的挂载点上！
            EquippedWeapon->WeaponMesh_3P->AttachToComponent(
                ArcadeChar->WeaponMountPoint,
                FAttachmentTransformRules::SnapToTargetNotIncludingScale
            );

            // 坐标归零，完美贴合挂载点
            EquippedWeapon->WeaponMesh_3P->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        }
    }
}

void UVRWeaponComponent_Arcade::GetShootOriginAndDirection(FVector& OutStartLoc, FVector& OutForwardDir, FVector& OutUpDir)
{
    //机台模式核心：射击起点永远是实体武器的枪管 (3P模型的枪口插槽)！不再是眼睛！
    if (EquippedWeapon && EquippedWeapon->ProjectileSpawn_3P)
    {
        OutStartLoc = EquippedWeapon->ProjectileSpawn_3P->GetComponentLocation();
        OutForwardDir = EquippedWeapon->ProjectileSpawn_3P->GetForwardVector();
        OutUpDir = EquippedWeapon->ProjectileSpawn_3P->GetUpVector();
    }
    else
    {
        Super::GetShootOriginAndDirection(OutStartLoc, OutForwardDir, OutUpDir);
    }
}

