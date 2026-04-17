// Fill out your copyright notice in the Description page of Project Settings.

#include "VRGun/Widget/VRAimWidget.h"
#include "Components/Image.h"
#include "Math/UnrealMathUtility.h"

#pragma region /* 生命周期与每帧更新 */

/**
 * UI 每帧更新逻辑
 * 负责处理基于时间的脉冲动画（扩散/收缩），以及根据不同武器类型（普通、水平射线、闪电链）
 * 计算并应用准星的位移、缩放与高频抖动（Jitter）效果。
 */
void UVRAimWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    float JitterX = 0.0f;
    float JitterY = 0.0f;

    // ==========================================
    // 1. 脉冲动画的时间轴计算 (射击时的扩散与回弹表现)
    // ==========================================
    float PulseSpreadMultiplier = 0.0f;
    if (bIsPulsing)
    {
        PulseTime += InDeltaTime;
        if (PulseTime >= PulseDuration)
        {
            PulseTime = PulseDuration;
            bIsPulsing = false; // 动画周期结束
        }

        // 计算当前动画进度的归一化值 (0.0 ~ 1.0)
        float Alpha = PulseTime / PulseDuration;

        // 拆分动画曲线：前 15% 时间快速扩散，后 85% 时间平滑回弹
        if (Alpha < 0.15f)
        {
            float LocalAlpha = Alpha / 0.15f;
            PulseSpreadMultiplier = FMath::InterpEaseOut(0.0f, 1.0f, LocalAlpha, 2.0f);
        }
        else
        {
            float LocalAlpha = (Alpha - 0.15f) / 0.85f;
            PulseSpreadMultiplier = FMath::InterpEaseIn(1.0f, 0.0f, LocalAlpha, 2.0f);
        }
    }

    // ==========================================
    // 2. 根据武器类型分发具体的视觉动画表现
    // ==========================================
    if (CurrentWeaponType == EVRWeaponType::HorizontalLine)
    {
        // 【水平射线枪】：左右拉扯表现
        CurrentSpread = MinSpread + (MaxSpread - MinSpread) * PulseSpreadMultiplier;

        // 当扩散程度超过设定阈值时，加入高频随机抖动，模拟武器后坐力导致的手部震颤
        if (CachedFireRate > 0.0f && PulseSpreadMultiplier >= JitterThreshold)
        {
            JitterX = FMath::RandRange(-JitterIntensity, JitterIntensity);
            JitterY = FMath::RandRange(-JitterIntensity, JitterIntensity);
        }

        if (Image_Horizontal_Left)
            Image_Horizontal_Left->SetRenderTranslation(FVector2D(-CurrentSpread + JitterX, JitterY));

        if (Image_Horizontal_Right)
            Image_Horizontal_Right->SetRenderTranslation(FVector2D(CurrentSpread + JitterX, JitterY));
    }
    else if (CurrentWeaponType == EVRWeaponType::ChainGun)
    {
        // 【连锁闪电枪】：整体放大并附带剧烈抖动
        // 将 0.0~1.0 的动画进度映射到目标缩放比例
        float ScaleAmount = 1.0f + (PulseSpreadMultiplier * ChainGunMaxScaleAdd);

        if (CachedFireRate > 0.0f && PulseSpreadMultiplier >= JitterThreshold)
        {
            JitterX = FMath::RandRange(-JitterIntensity, JitterIntensity);
            JitterY = FMath::RandRange(-JitterIntensity, JitterIntensity);
        }

        if (Image_ChainGun)
        {
            // 利用 SetRenderScale 实现整体大小的呼吸感
            Image_ChainGun->SetRenderScale(FVector2D(ScaleAmount, ScaleAmount));
            // 叠加高频位移抖动，增强闪电爆发时的狂躁感
            Image_ChainGun->SetRenderTranslation(FVector2D(JitterX, JitterY));
        }
    }
    else
    {
        // 【常规武器】：经典的十字准星四向扩散逻辑
        float TargetSpread = bIsFiring ? MaxSpread : MinSpread;
        float InterpSpeed = bIsFiring ? SpreadSpeed : ShrinkSpeed;

        // 使用 FInterpTo 实现平滑的插值过渡
        CurrentSpread = FMath::FInterpTo(CurrentSpread, TargetSpread, InDeltaTime, InterpSpeed);

        if (CachedFireRate > 0.0f && bIsFiring && CurrentSpread >= (MaxSpread * JitterThreshold))
        {
            JitterX = FMath::RandRange(-JitterIntensity, JitterIntensity);
            JitterY = FMath::RandRange(-JitterIntensity, JitterIntensity);
        }

        // 分别向四个方向推开准星的四个独立图片部件
        if (Image_Top)    Image_Top->SetRenderTranslation(FVector2D(JitterX, -CurrentSpread + JitterY));
        if (Image_Bottom) Image_Bottom->SetRenderTranslation(FVector2D(JitterX, CurrentSpread + JitterY));
        if (Image_Left)   Image_Left->SetRenderTranslation(FVector2D(-CurrentSpread + JitterX, JitterY));
        if (Image_Right)  Image_Right->SetRenderTranslation(FVector2D(CurrentSpread + JitterX, JitterY));
    }
}

#pragma endregion


#pragma region /* 状态控制与接口 */

/**
 * 设置持续开火状态
 * 主要用于常规武器判定是否需要维持在最大扩散状态。
 */
void UVRAimWidget::SetFiringState(bool bFiring)
{
    bIsFiring = bFiring;
}

/**
 * 切换准星类型
 * 根据当前装备的武器类型，动态控制 UI 控件的可见性以改变准星形态。
 */
void UVRAimWidget::UpdateCrosshairType(EVRWeaponType InWeaponType)
{
    CurrentWeaponType = InWeaponType;

    // 状态判定
    bool bIsHorizontal = (CurrentWeaponType == EVRWeaponType::HorizontalLine);
    bool bIsChainGun = (CurrentWeaponType == EVRWeaponType::ChainGun);
    bool bIsNormal = (!bIsHorizontal && !bIsChainGun);

    // 【性能优化】：使用 SelfHitTestInvisible 替代 Visible，避免准星拦截玩家的 UI 射线交互
    ESlateVisibility NormalVis = bIsNormal ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden;
    ESlateVisibility HorizVis = bIsHorizontal ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden;
    ESlateVisibility ChainVis = bIsChainGun ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden;

    // 切换常规十字准星部件
    if (Image_Top)    Image_Top->SetVisibility(NormalVis);
    if (Image_Bottom) Image_Bottom->SetVisibility(NormalVis);
    if (Image_Left)   Image_Left->SetVisibility(NormalVis);
    if (Image_Right)  Image_Right->SetVisibility(NormalVis);

    // 切换水平射线准星部件
    if (Image_Horizontal_Left)  Image_Horizontal_Left->SetVisibility(HorizVis);
    if (Image_Horizontal_Right) Image_Horizontal_Right->SetVisibility(HorizVis);

    // 切换连锁闪电准星部件
    if (Image_ChainGun) Image_ChainGun->SetVisibility(ChainVis);
}

/**
 * 触发一次单发脉冲动画
 * 根据传入的射速（FireRate）自动计算动画总时长，确保表现与射击节奏完美契合。
 */
void UVRAimWidget::PlayFirePulse(float FireRate)
{
    // 每次开火时缓存当前射速
    CachedFireRate = FireRate;
    // 计算单发子弹的时间间隔。
    // 乘以 0.95 是为了预留 5% 的时间余量，绝对保证在下一发打出前准星的视觉状态已经完美重置收缩。
    PulseDuration = (FireRate > 0.0f) ? (1.0f / FireRate) * 0.95f : 0.1f;
    PulseTime = 0.0f;
    bIsPulsing = true;
}

#pragma endregion