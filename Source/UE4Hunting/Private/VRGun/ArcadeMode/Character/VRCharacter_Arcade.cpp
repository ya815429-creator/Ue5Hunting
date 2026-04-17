// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/ArcadeMode/Character/VRCharacter_Arcade.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "VRGun/Prop/VRAimActor.h"
#include "Net/UnrealNetwork.h"

// ==========================================
// 1. 初始化与生命周期
// ==========================================
AVRCharacter_Arcade::AVRCharacter_Arcade()
{
    PrimaryActorTick.bCanEverTick = true;

    // 1. 创建并设置机台武器挂载点
    WeaponMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponMountPoint"));
    WeaponMountPoint->SetupAttachment(RootComponent);
    WeaponMountPoint->SetRelativeLocation(FVector(40.0f, 0.0f, 40.0f));

    // 2. 清理遗留系统的视觉干扰
    if (GunMesh_1P)
    {
        GunMesh_1P->SetHiddenInGame(true);
        GunMesh_1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    if (GetMesh())
    {
        GetMesh()->SetOwnerNoSee(false); // 允许低头看自己的身体
    }

    // 3. 切断父类的躯干跟随头部旋转逻辑，保持机台底盘固定
    bAutoRotateMeshWithHead = false;
}

void AVRCharacter_Arcade::BeginPlay()
{
    Super::BeginPlay();

    // 1. 隐藏本地玩家头部，实现“无头骑士”防遮挡
    if (IsLocallyControlled() && GetMesh())
    {
        GetMesh()->HideBoneByName(HeadBoneName, PBO_None);
    }

    // 2. 将准星从头显剥离，强制吸附至实体机台枪管正前方
    if (IsLocallyControlled() && LocalVRAimActor && WeaponMountPoint)
    {
        LocalVRAimActor->AttachToComponent(WeaponMountPoint, FAttachmentTransformRules::KeepRelativeTransform);
        LocalVRAimActor->SetActorRelativeLocation(FVector(1000.f, 0.f, 0.f));
        LocalVRAimActor->SetActorRelativeRotation(FRotator(0.f, 180.f, 0.f));
    }
}

// ==========================================
// 2. 核心运转与网络分发 (Tick)
// ==========================================
void AVRCharacter_Arcade::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. 物理表现：平滑驱动挂载点旋转 (全网所有人都会执行这一步)
    if (WeaponMountPoint)
    {
        CurrentWeaponRotation = FMath::RInterpTo(CurrentWeaponRotation, TargetWeaponRotation, DeltaTime, WeaponRotationInterpSpeed);
        WeaponMountPoint->SetRelativeRotation(CurrentWeaponRotation);
    }

    // 2. 网络分发：仅本地主控玩家负责向服务器汇报
    if (IsLocallyControlled())
    {
        if (!HasAuthority())
        {
            // 客机节流发送 RPC
            float CurrentTime = GetWorld()->GetTimeSeconds();
            if (CurrentTime - LastWeaponSyncTime > SYNC_INTERVAL)
            {
                Server_SetArcadeWeaponRotation(TargetWeaponRotation);
                LastWeaponSyncTime = CurrentTime;
            }
        }
        else
        {
            // 主机直接赋值给同步变量
            Rep_ArcadeWeaponRotation = TargetWeaponRotation;
        }
    }
}

// ==========================================
// 3. 硬件控制接口 (蓝图调用)
// ==========================================
void AVRCharacter_Arcade::UpdateArcadeWeaponRotation(float InPitch, float InYaw)
{
    // 🌟 终极防弹设计：非本地玩家一律拦截！
    // 彻底解决蓝图中 Server 错误覆盖客户端数据的问题
    if (!IsLocallyControlled()) return;

    // 限制物理死角并更新目标
    float ClampedPitch = FMath::Clamp(InPitch, MinPitch, MaxPitch);
    float ClampedYaw = FMath::Clamp(InYaw, MinYaw, MaxYaw);

    TargetWeaponRotation = FRotator(ClampedPitch, ClampedYaw, 0.0f);
}

// ==========================================
// 4. 网络同步具体实现
// ==========================================
void AVRCharacter_Arcade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AVRCharacter_Arcade, Rep_ArcadeWeaponRotation);
}

bool AVRCharacter_Arcade::Server_SetArcadeWeaponRotation_Validate(FRotator NewRot) { return true; }

void AVRCharacter_Arcade::Server_SetArcadeWeaponRotation_Implementation(FRotator NewRot)
{
    Rep_ArcadeWeaponRotation = NewRot; // 下发给客机
    TargetWeaponRotation = NewRot;     // 服务器自己也要更新目标
}

void AVRCharacter_Arcade::OnRep_ArcadeWeaponRotation()
{
    // 远端客机收到数据后，仅更新目标，由 Tick 去平滑追赶
    TargetWeaponRotation = Rep_ArcadeWeaponRotation;
}