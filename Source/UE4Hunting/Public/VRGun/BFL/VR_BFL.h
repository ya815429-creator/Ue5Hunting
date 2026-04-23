// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VR_BFL.generated.h"

/**
 * 
 */
UCLASS()
class UE4HUNTING_API UVR_BFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * 批量流送和卸载关卡，并在所有任务完成后触发 Completed 引脚。
	 * @param LevelsToStream   需要加载并设置为可见的关卡名称列表
	 * @param LevelsToUnload   需要卸载的关卡名称列表
	 */
	UFUNCTION(BlueprintCallable, Category = "VR|Level", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", Keywords = "Stream Load Unload Batch"))
	static void StreamLevelsBatch(const UObject* WorldContextObject,TArray<FName> LevelsToStream,TArray<FName> LevelsToUnload,FLatentActionInfo LatentInfo);
};
