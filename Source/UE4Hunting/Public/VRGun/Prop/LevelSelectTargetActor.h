// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelSelectTargetActor.generated.h"

UCLASS()
class UE4HUNTING_API ALevelSelectTargetActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ALevelSelectTargetActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:

    //碰撞体，用于玩家检测
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class USphereComponent* CollisionComp;

    //静态模型，用于显示
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UStaticMeshComponent* MeshComp;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Select")
    class USkeletalMeshComponent* SkeletalMesh;

    /** 绑定的关卡名称 (被打爆后将把这个名字传给 GameState) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Select")
    FName TargetLevelName;

    /** 重写受击函数，接收玩家武器射线的伤害 */
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

    /** 靶子被打时的表现*/
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayHitEffect();

private:
    //是否已被触发
    bool bHasBeenTriggered = false;

};
