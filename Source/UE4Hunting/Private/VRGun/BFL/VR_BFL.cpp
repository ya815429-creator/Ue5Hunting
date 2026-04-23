// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/BFL/VR_BFL.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
class FStreamLevelsBatchLatentAction :public FPendingLatentAction
{
public:
	TArray<ULevelStreaming*> StreamingLevels; // 正在加载的关卡
	TArray<ULevelStreaming*> UnloadingLevels; // 正在卸载的关卡

	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;

	FStreamLevelsBatchLatentAction(const FLatentActionInfo& LatentInfo)
		: ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
	{
	}
	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		bool bIsAllFinished = true;

		// 检查加载队列：必须全部 Loaded 且 Visible
		for (ULevelStreaming* LS : StreamingLevels)
		{
			if (LS && (!LS->IsLevelLoaded() || !LS->IsLevelVisible()))
			{
				bIsAllFinished = false;
				break;
			}
		}

		if (bIsAllFinished)
		{
			// 检查卸载队列：必须全部 Unloaded (Not Loaded)
			for (ULevelStreaming* LS : UnloadingLevels)
			{
				if (LS && LS->IsLevelLoaded())
				{
					bIsAllFinished = false;
					break;
				}
			}
		}

		Response.FinishAndTriggerIf(bIsAllFinished, ExecutionFunction, OutputLink, CallbackTarget);
	}
};

void UVR_BFL::StreamLevelsBatch(const UObject* WorldContextObject, TArray<FName> LevelsToStream, TArray<FName> LevelsToUnload, FLatentActionInfo LatentInfo)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World) return;

	FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
	if (LatentActionManager.FindExistingAction<FStreamLevelsBatchLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID))
	{
		return; // 防止同一节点被重复触发
	}

	// 创建并注册潜行动作
	FStreamLevelsBatchLatentAction* Action = new FStreamLevelsBatchLatentAction(LatentInfo);

	// 1. 发起加载流送任务
	for (const FName& LName : LevelsToStream)
	{
		ULevelStreaming* LS = UGameplayStatics::GetStreamingLevel(WorldContextObject, LName);
		if (LS)
		{
			LS->SetShouldBeLoaded(true);
			LS->SetShouldBeVisible(true); // 对应 MakeVisibleAfterLoad = true
			Action->StreamingLevels.Add(LS);
		}
	}

	// 2. 发起卸载任务
	for (const FName& LName : LevelsToUnload)
	{
		ULevelStreaming* LS = UGameplayStatics::GetStreamingLevel(WorldContextObject, LName);
		if (LS)
		{
			LS->SetShouldBeLoaded(false);
			LS->SetShouldBeVisible(false);
			Action->UnloadingLevels.Add(LS);
		}
	}

	LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, Action);
}
