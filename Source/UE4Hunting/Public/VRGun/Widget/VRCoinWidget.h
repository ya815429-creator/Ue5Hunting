// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VRCoinWidget.generated.h"

/**
 * 
 */
UCLASS()
class UE4HUNTING_API UVRCoinWidget : public UUserWidget
{
	GENERATED_BODY()
public:
    /** * 核心事件：当投币数据发生变化时，C++ 会自动触发这个节点！
     * @param CurrentCoins 当前已投的散币 (例如 1)
     * @param RequiredCoins 激活所需的总币数 (例如 3)
     * @param Credits 剩余可用信用点 (可续玩的局数)
     * @param bIsPaid 是否已经激活 (如果为 True，UI可显示为"请按扳机发车")
     * @param bIsFreeMode 是否开启了展会免费模式
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "VR|Arcade UI")
    void OnCoinDataUpdated(int32 Index, int32 CurrentCoins, int32 RequiredCoins, int32 Credits, bool bIsPaid, bool bIsFreeMode);
};
