// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRGun/Interface/IBindableEntity.h"
#include "BindSequenceActor.generated.h"

UCLASS()
class UE4HUNTING_API ABindSequenceActor : public AActor, public IIBindableEntity
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABindSequenceActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region /* 序列同步与接口 */
public:
	/** * 本角色的动态绑定标识 (例如 "Player_0", "Player_1")
	 * 根据服务器下发的 AssignedPlayerIndex 动态生成。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Sequence")
	FName MyBindingTag;

	/** * 序列同步组件
 * 负责解析和执行关卡序列(Level Sequence)中的 Actor 绑定逻辑。
 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
	class USequenceSyncComponent* SyncComp;

	/** 实现 IBindableEntity 接口：返回当前角色的绑定 Tag */
	virtual FName GetBindingTag() const override { return MyBindingTag; }

	/** 实现 IBindableEntity 接口：返回自身的序列同步组件指针 */
	virtual USequenceSyncComponent* GetSequenceSyncComponent() const override { return SyncComp; }
#pragma endregion
};
