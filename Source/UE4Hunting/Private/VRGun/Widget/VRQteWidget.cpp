// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Widget/VRQteWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Image.h" 
#include "Materials/MaterialInstanceDynamic.h"
void UVRQteWidget::InitProperty(float InSecond, int32 InLevel)
{
	// 对应蓝图里的 Set Second 节点
	Second = InSecond;

	// 对应蓝图里的 Set Level 节点
	Level = InLevel;
}

void UVRQteWidget::InitAnim()
{
	// 1. 计算播放速度： 1.0 / Second
	// C++ 独家安全锁：防止 Second 被错误地传成 0 导致游戏除以 0 崩溃！
	float PlaybackSpeed = (Second > 0.0f) ? (1.0f / Second) : 1.0f;

	// 2. 先无条件播放基础的 Turn 和 Appear 动画
	if (Turn)
	{
		PlayAnimation(Turn, 0.0f, 1, EUMGSequencePlayMode::Forward, PlaybackSpeed);
	}
	if (Appear)
	{
		PlayAnimation(Appear, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
	}

	// 3. 根据 QTE 的 Level 决定是否播放额外的叠加动画
	switch (Level)
	{
	case 1:
		// Level 1 只有基础动画，无需额外操作
		break;
	case 2:
		// Level 2 额外播放 Appear2
		if (Appear2)
		{
			PlayAnimation(Appear2, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
		}
		break;
	case 3:
		// Level 3 额外播放 Appear3
		if (Appear3)
		{
			PlayAnimation(Appear3, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
		}
		break;
	default:
		// 级别如果是 0 或者没配置，触发原来的那条打印报错逻辑
		UE_LOG(LogTemp, Error, TEXT("Error：qte Level Is Error！！！！"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Error：qte Level Is Error！！！！"));
		}
		break;
	}
}

void UVRQteWidget::OnDamaged(float InValue)
{
	// 1. 将传入的值赋给 Alpha 变量 
	Alpha = InValue;

	// 2. 提取 Q2 图片的动态材质，并修改参数
	if (Q2)
	{
		// GetDynamicMaterial 会自动获取或创建一个动态材质实例，和蓝图节点一模一样
		UMaterialInstanceDynamic* DynMat = Q2->GetDynamicMaterial();
		if (DynMat)
		{
			// 对应蓝图的 Set Scalar Parameter Value 节点
			DynMat->SetScalarParameterValue(FName("Alpha"), Alpha);
		}
	}

	// 3. 播放受击动画 Damage
	if (Damage)
	{
		// 参数：动画对象, 起始时间, 播放次数(1), 正向播放, 正常倍速(1.0)
		PlayAnimation(Damage, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
	}
}

void UVRQteWidget::OnDie(int32 State)
{
	// 准备一个临时的动画指针，用来接收我们选中的动画
	UWidgetAnimation* AnimToPlay = nullptr;

	// 对应蓝图里的 Select 节点
	switch (State)
	{
	case 0:
		AnimToPlay = Disappear;
		break;
	case 1:
		AnimToPlay = Disa2;
		break;
	case 2:
		AnimToPlay = Disa3;
		break;
	default:
		// 防错处理：如果传了 3、4、5 这种没有配置的值，打印个警告
		UE_LOG(LogTemp, Warning, TEXT("[VRQteWidget] OnDie 传入了未知的 State: %d"), State);
		break;
	}

	// 如果成功选中了动画，并且动画不为空，就播放它
	if (AnimToPlay)
	{
		// 参数：动画对象, 起始时间, 播放次数(1), 正向播放, 正常倍速(1.0)
		PlayAnimation(AnimToPlay, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
	}
}

void UVRQteWidget::ToWhite()
{
	// 安全校验：如果动画存在就播放它
	if (ToWhiteAnim)
	{
		// 参数：动画对象, 起始时间, 播放次数(1), 正向播放, 正常倍速(1.0)
		PlayAnimation(ToWhiteAnim, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f);
	}
}