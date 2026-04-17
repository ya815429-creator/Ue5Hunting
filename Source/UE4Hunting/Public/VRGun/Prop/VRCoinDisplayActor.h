// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRCoinDisplayActor.generated.h"
class UWidgetComponent;
UCLASS()
class UE4HUNTING_API AVRCoinDisplayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVRCoinDisplayActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
    /** 挂载 UI 屏幕的组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UWidgetComponent* WidgetComponent;

    /** 核心配置：这个屏幕是给谁看的？ 0 代表 P0，1 代表 P1 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Arcade UI")
    int32 TargetPlayerIndex = 0;

private:
    // 用于缓存上一次的数据，只有数据真正改变时才刷新 UI，极致节省性能！
    int32 LastCoins;
    int32 LastCredits;
    bool bLastPaid;
    bool bLastFreeMode;
};
