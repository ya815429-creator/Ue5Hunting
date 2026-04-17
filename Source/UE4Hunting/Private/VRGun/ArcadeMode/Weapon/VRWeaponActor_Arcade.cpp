// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/ArcadeMode/Weapon/VRWeaponActor_Arcade.h"


AVRWeaponActor_Arcade::AVRWeaponActor_Arcade()
{

}



void AVRWeaponActor_Arcade::FireVisuals(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, bool bIsLocalFire, EVRWeaponType WeaponType)
{
    // ==========================================
    // 核心拦截机制：
    // 在机台模式中，没有“你的枪”和“别人的枪”的区别，场景里只有一把挂在角色胸前的 3P 实体枪。
    // 因此，无论是不是本地玩家开火，我们强制传入 `false`。
    // 这样父类的代码就会乖乖地使用 MuzzleFlashLoc_3P 和 ProjectileSpawn_3P 来生成火焰和灯光！
    // ==========================================
    Super::FireVisuals(TargetLocations, TargetNormals, HitTypes, false, WeaponType);
}



