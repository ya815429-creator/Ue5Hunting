// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRDamageNumberActor.generated.h"
UCLASS()
class UE4HUNTING_API AVRDamageNumberActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVRDamageNumberActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UTextRenderComponent* TextComponent;

    void InitDamageText(float InDamage, FVector HitLocation);

protected:
    // ==================================================
    // 基础设置 (Base Settings)
    // ==================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Base")
    float MaxLifeTime = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Base")
    float FloatSpeed = 100.0f;

    /** 初始生成的额外高度偏移 (向上抬高，防止打脚时数字从地下冒出来) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Base")
    float InitialZOffset = 40.0f; // 默认往上抬 40 厘米

    /** 【新增】左右随机偏移的最大距离 (只在玩家视角的水平面上散布，单位: 厘米) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Base")
    float RandomOffsetRightMax = 25.0f;

    /** 【新增】向上随机偏移的最大距离 (只会往上加，绝对不往下掉，单位: 厘米) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Base")
    float RandomOffsetUpMax = 20.0f;

    // ==================================================
    // 假数值附加区间配置 (Extra Value Ranges)
    // ==================================================
    /** 普通伤害的随机附加值上限 (例如传进10，这里填40，普通区间就是 10~50) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Value")
    int32 NormalExtraMax = 40;

    /** 暴击伤害的随机附加值下限 (例如传进10，这里填90，暴击最低就是 100) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Value")
    int32 CritExtraMin = 90;

    /** 暴击伤害的随机附加值上限 (例如传进10，这里填1490，暴击最高就是 1500) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Value")
    int32 CritExtraMax = 1490;

    // ==================================================
    // 摇摆动效 (Shake Animation)
    // ==================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Animation")
    float ShakeProbability = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Animation")
    float ShakeDuration = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Animation")
    float ShakeMagnitude = 40.0f;

    // ==================================================
    // 颜色抽奖配置 (Color Settings)
    // ==================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RedColorProb = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Color")
    FColor RedColor = FColor::Red;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float YellowColorProb = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Color")
    FColor YellowColor = FColor::Yellow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageNumber|Color")
    FColor NormalColor = FColor::White;

    // ----------------- 内部运行状态 -----------------
    float CurrentLifeTime = 0.0f;
    bool bIsShaking = false;
    FColor BaseColor;

    UPROPERTY(Transient)
    class APlayerCameraManager* CachedCameraManager;
};
