// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/GameInstanceSubsystem/DlssGameInstanceSubsystem.h"
#include "StreamlineLibraryReflex.h"
#include "StreamlineRHI.h"
#include "DLSSLibrary.h"
#include "Engine/Engine.h"
#include "UnrealEngine.h"
#include "StreamlineLibraryDLSSG.h"
#include "GameFramework/GameUserSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
void UDlssGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 在游戏启动时自动应用最佳设置
	/*ApplyOptimalDLSSSettings();*/
}

void UDlssGameInstanceSubsystem::ApplyOptimalDLSSSettings()
{
	// ==========================================
	// 【联机适配】专属服务器拦截
	// ==========================================
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// ==========================================
	// 【VR 智能侦测】
	// ==========================================
	bool bIsVRMode = false;
	if (FCommandLine::Get() && FString(FCommandLine::Get()).Contains(TEXT("-vr")))
	{
		bIsVRMode = true;
	}
	if (!bIsVRMode && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		bIsVRMode = true;
	}
	if (!bIsVRMode && GEngine && GEngine->IsStereoscopic3D())
	{
		bIsVRMode = true;
	}

	// ==========================================
	// 1. 系统级前置参数
	// ==========================================
	// 即使没有光追，DLSS-SR 和 帧生成也需要运动矢量
	GEngine->Exec(World, TEXT("r.Velocity.ForceOutput 1"));
	// 保护 UI 免受插帧带来的抖动
	GEngine->Exec(World, TEXT("r.Streamline.TagUIColorAlpha 1"));
	GEngine->Exec(World, TEXT("r.Streamline.ClearSceneColorAlpha 1"));

	// ==========================================
	// 2. NVIDIA Reflex 与 DLSS 帧生成 (FG/MFG)
	// ==========================================
	if (bIsVRMode)
	{
		// VR 模式下：必须彻底关闭帧生成，防止渲染冲突报错
		GEngine->Exec(World, TEXT("r.Streamline.DLSSG.Enable 0"));
		GEngine->Exec(World, TEXT("r.Streamline.Reflex.Enable 0"));
	}
	else
	{
		// PC 模式下：帧生成即使在非光追场景下也能极大提升流畅度
		GEngine->Exec(World, TEXT("r.Streamline.Reflex.Enable 2"));

		if (UStreamlineLibraryDLSSG::IsDLSSGSupported())
		{
			UStreamlineLibraryDLSSG::SetDLSSGMode(EStreamlineDLSSGMode::Auto);
			GEngine->Exec(World, TEXT("r.vsync 0"));
		}
	}

	// ==========================================
	// 3. NVIDIA 图像缩放 (NIS) - 后处理锐化器
	// ==========================================
	// 补足 DLSS 造成的微小发软。
	GEngine->Exec(World, TEXT("r.NIS.Enable 1"));

	// ==========================================
	// 4. DLSS 超分辨率 (DLSS-SR)
	// ==========================================
	if (UDLSSLibrary::IsDLSSSupported())
	{
		// 开启基础 DLSS 提升帧率
		UDLSSLibrary::EnableDLSS(true);

		// --- 【修改点】移除了所有光线重建 (RR) 相关的 DenoiserMode 设置 ---
		// --- 同时移除了对 Lumen/Shadow 降噪器的强制关闭命令，让它们恢复正常工作 ---

		FIntPoint CurrentRes = GEngine->GetGameUserSettings()->GetScreenResolution();
		FVector2D ScreenRes(CurrentRes.X, CurrentRes.Y);

		bool bIsModeSupported = false;
		float OptimalScreenPercentage = 100.0f;
		bool bIsFixedScreenPercentage = false;
		float MinScreenPercentage = 100.0f;
		float MaxScreenPercentage = 100.0f;
		float OptimalSharpness = 0.0f;

		UDLSSMode SelectedMode = UDLSSLibrary::GetDefaultDLSSMode();

		// 适配 5.5 的 8 参数签名
		UDLSSLibrary::GetDLSSModeInformation(
			SelectedMode,
			ScreenRes,
			bIsModeSupported,
			OptimalScreenPercentage,
			bIsFixedScreenPercentage,
			MinScreenPercentage,
			MaxScreenPercentage,
			OptimalSharpness
		);

		if (bIsModeSupported)
		{
			FString Command = FString::Printf(TEXT("r.ScreenPercentage %f"), OptimalScreenPercentage);
			GEngine->Exec(World, *Command);
		}
	}
}
