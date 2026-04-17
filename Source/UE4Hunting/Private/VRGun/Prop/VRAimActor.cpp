#include "VRGun/Prop/VRAimActor.h"
#include "Components/WidgetComponent.h"
#include "VRGun/Widget/VRAimWidget.h"

#pragma region /* 初始化与构建 */

/**
 * 构造函数：构建 3D 准星载体实体。
 * 由于准星只是第一人称视觉辅助元素，因此该 Actor 纯本地生成且绝不允许网络同步。
 */
AVRAimActor::AVRAimActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false; // 严禁联机同步，避免网络开销

    // 创建并配置 Widget 组件以在 3D 空间中承载 UMG UI
    WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
    RootComponent = WidgetComp;
}

#pragma endregion


#pragma region /* UI 状态控制接口 */

/**
 * 通知底层的 UMG Widget 切换开火扩散状态
 */
void AVRAimActor::SetFiringState(bool bFiring)
{
    if (WidgetComp)
    {
        UVRAimWidget* VRAimWidget = Cast<UVRAimWidget>(WidgetComp->GetUserWidgetObject());
        if (VRAimWidget)
        {
            VRAimWidget->SetFiringState(bFiring);
        }
    }
}

/**
 * 通知底层的 UMG Widget 切换准星的具体样式（根据当前获取的武器类型）
 */
void AVRAimActor::UpdateCrosshairType(EVRWeaponType InWeaponType)
{
    if (WidgetComp)
    {
        UVRAimWidget* VRAimWidget = Cast<UVRAimWidget>(WidgetComp->GetUserWidgetObject());
        if (VRAimWidget)
        {
            VRAimWidget->UpdateCrosshairType(InWeaponType);
        }
    }
}

/**
 * 触发一次单发脉冲扩散动画（供点射型武器或有独立节奏的武器使用）
 */
void AVRAimActor::PlayFirePulse(float FireRate)
{
    if (WidgetComp)
    {
        UVRAimWidget* VRAimWidget = Cast<UVRAimWidget>(WidgetComp->GetUserWidgetObject());
        if (VRAimWidget)
        {
            VRAimWidget->PlayFirePulse(FireRate);
        }
    }
}

#pragma endregion