#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VRAnimInstance_Gun.generated.h"

/**
 * VR 枪械角色专属动画实例
 * 负责提取网络同步传来的头显(HMD)变换数据，执行防抖动插值，并为动画蓝图中的瞄准偏移(AimOffset)提供参数。
 */
UCLASS()
class UE4HUNTING_API UVRAnimInstance_Gun : public UAnimInstance
{
    GENERATED_BODY()

#pragma region /* 动画初始化与更新 */
public:
    /**
     * 动画实例初始化回调
     * 架构要点：使用底层的 GetOwningActor 进行指针缓存，防止关卡定序器剥夺 Pawn 状态时导致的指针失效。
     */
    virtual void NativeInitializeAnimation() override;

    /**
     * 每一帧更新动画数据
     * 执行远端角色表现的核心平滑插值逻辑
     */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
#pragma endregion


#pragma region /* 动画驱动变量 */
public:
    /** 最终传递给动画蓝图 (AnimGraph) 状态机的平滑头部变换值 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|Sync")
    FTransform SmoothHeadTransform;

    /** 提取出供 Aim Offset (瞄准偏移) 驱动角色躯干俯仰表现的 Pitch 角 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|Sync")
    float AimPitch;
#pragma endregion


#pragma region /* 参数配置与内部缓存 */
protected:
    /**
     * 插值平滑速度
     * 决定远端模型头部运动的滞后感。数值越高，动作越紧跟网络数据但越容易引发画面抖动；
     * 局域网机台环境无惧高延迟，建议配置在 15.0 - 25.0 之间以获取最佳表现。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VR|Settings")
    float InterpolationSpeed = 20.0f;

    /** 缓存的所属角色实体指针，避免每帧执行开销巨大的类型转换操作 */
    UPROPERTY(Transient)
    class AVRCharacter_Gun* CachedCharacter;
#pragma endregion
};