#include "VRGun/VRAnimInstance_Gun/VRAnimInstance_Gun.h"
#include "VRGun/VRCharacter_Gun.h" 
#include "Kismet/KismetMathLibrary.h"

#pragma region /* 动画实例初始化与更新 */

/**
 * 动画初始化回调
 */
void UVRAnimInstance_Gun::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    // 【架构注意】：
    // 使用 GetOwningActor() 直接获取网格体挂载的 Actor 并进行类型转换。
    // 不要使用通常的 TryGetPawnOwner()。因为一旦角色被绑定到关卡定序器 (Level Sequence)，
    // 引擎底层可能会剥夺其 Pawn 身份控制权，导致获取失败。
    CachedCharacter = Cast<AVRCharacter_Gun>(GetOwningActor());
}

/**
 * 动画每帧更新回调
 * 处理网络同步传来的头部变换数据，通过平滑插值解决网络抖动，计算出最终的视角 Pitch。
 */
void UVRAnimInstance_Gun::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // 1. 尝试获取缓存。若之前由于时序问题未能成功初始化，则在此处重试。
    if (!CachedCharacter)
    {
        CachedCharacter = Cast<AVRCharacter_Gun>(GetOwningActor());
        if (!CachedCharacter) return;
    }

    // 2. 从缓存的角色实体中提取已经通过网络同步机制传来的最新头显(HMD)基准数据
    FTransform NetTarget = CachedCharacter->GetSyncedHeadTransform();

    // 判定当前运行环境是否为专用服务器
    bool bIsDedicatedServer = (GetWorld()->GetNetMode() == NM_DedicatedServer);

    // 3. 核心平滑插值计算
    // 本地主控玩家（无延迟）和专用服务器（无需视觉呈现）不需要进行插值平滑，
    // 仅针对非本地控制的远端玩家（即别人的 3P 模型）执行平滑。
    if (!CachedCharacter->IsLocallyControlled() && !bIsDedicatedServer)
    {
        // 确保插值速度永远不为 0，防止除零错误或计算挂起
        float SafeInterpSpeed = FMath::Max(0.1f, InterpolationSpeed);

        FVector CurrentLoc = SmoothHeadTransform.GetLocation();
        FRotator CurrentRot = SmoothHeadTransform.Rotator();

        FVector TargetLoc = NetTarget.GetLocation();
        FRotator TargetRot = NetTarget.Rotator();

        // 使用 Kismet 数学库分别对位置和旋转进行平滑过度
        FVector NewLoc = UKismetMathLibrary::VInterpTo(CurrentLoc, TargetLoc, DeltaSeconds, SafeInterpSpeed);
        FRotator NewRot = UKismetMathLibrary::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, SafeInterpSpeed);

        SmoothHeadTransform = FTransform(NewRot, NewLoc, NetTarget.GetScale3D());
    }
    else
    {
        // 本地或专服直接采纳网络目标值，做到零延迟
        SmoothHeadTransform = NetTarget;
    }

    // 4. 提取最终平滑后的俯仰角 (Pitch)，并将其强制归一化到 (-180, 180) 范围内
    // 该值通常将传递给动画蓝图的状态机或 AimOffset，用于驱动角色的抬头/低头骨骼表现
    AimPitch = FRotator::NormalizeAxis(SmoothHeadTransform.Rotator().Pitch);
}

#pragma endregion