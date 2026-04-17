// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VRGun/Global/Enum.h" 
#include "VRAimWidget.generated.h"

class UImage;

/**
 * VR 动态准星 UI 控件
 * 负责解析不同武器类型的形态切换，并通过 NativeTick 实现平滑扩散、收缩及后坐力高频抖动（Jitter）动画。
 */
UCLASS()
class UE4HUNTING_API UVRAimWidget : public UUserWidget
{
    GENERATED_BODY()

#pragma region /* 生命周期与每帧更新 */
protected:
    /**
     * 覆盖 NativeTick 以执行每帧的动画插值逻辑
     */
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
#pragma endregion


#pragma region /* UI 控件绑定 */
protected:
    // ==========================================
    // 控件绑定声明：必须在对应的 UMG 蓝图中创建同名 Image 控件，否则将导致编译/运行报错！
    // ==========================================

    /** 常规十字准星部件 */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Top;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Bottom;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Left;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Right;

    /** 水平切割枪准星部件 */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Horizontal_Left;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    UImage* Image_Horizontal_Right;

    /** 连锁闪电枪准星部件 */
    UPROPERTY(meta = (BindWidget))
    UImage* Image_ChainGun;
#pragma endregion


#pragma region /* 外部控制接口 */
public:
    /** * 设置持续开火状态 (用于控制常规武器准星是否维持最大扩散)
     */
    void SetFiringState(bool bFiring);

    /** * 动态切换准星的可见形态组合
     */
    void UpdateCrosshairType(EVRWeaponType InWeaponType);

    /** * 接收单发开火脉冲，根据射速动态计算本次扩散/收缩动画的完整周期
     */
    void PlayFirePulse(float FireRate);
#pragma endregion


#pragma region /* 动画参数配置 */
protected:
    /** 不开火时的基础偏移/大小 */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings")
    float MinSpread = 0.0f;

    /** 开火时的最大偏移/大小 */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings")
    float MaxSpread = 40.0f;

    /** 准星扩散速度 (用于连续开火的平滑插值) */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings")
    float SpreadSpeed = 20.0f;

    /** 准星恢复收缩速度 (用于停火后的平滑插值) */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings")
    float ShrinkSpeed = 10.0f;

    /** 连锁闪电枪射击时触发的最大缩放增量 (如 0.5 表示放大至 1.5 倍) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair Settings|ChainGun")
    float ChainGunMaxScaleAdd = 0.5f;

    // ==========================================
    // 高频抖动参数 (Jitter)
    // ==========================================

    /** 抖动强度（像素）。数值越大，模拟后坐力导致的视觉震颤越剧烈 */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings|Jitter")
    float JitterIntensity = 3.0f;

    /** 触发抖动的动画进度阈值：1.0 表示到达 MaxSpread 才抖，0.9 表示到达 90% 扩散就开始抖 */
    UPROPERTY(EditAnywhere, Category = "Crosshair Settings|Jitter")
    float JitterThreshold = 0.9f;
   
#pragma endregion


#pragma region /* 内部状态缓存 */
private:
    bool bIsFiring = false;
    float CurrentSpread = 0.0f;
    EVRWeaponType CurrentWeaponType = EVRWeaponType::Normal;

    // 脉冲动画专用状态变量
    float PulseTime = 0.0f;
    float PulseDuration = 0.0f;
    bool bIsPulsing = false;

    /** 缓存当前武器的射速，用于判断是否屏蔽单发武器的抖动*/
    float CachedFireRate = 0.0f;
#pragma endregion
};