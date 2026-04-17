// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Prop/BindSequenceActor.h"
#include "VRGun/Component/SequenceSyncComponent.h"
// Sets default values
ABindSequenceActor::ABindSequenceActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// 开启网络同步，包括位置和旋转的移动同步
	bReplicates = true;

	// 创建序列同步组件，用于处理关卡序列（Level Sequence）的绑定与同步
	SyncComp = CreateDefaultSubobject<USequenceSyncComponent>(TEXT("SequenceSync"));
}

// Called when the game starts or when spawned
void ABindSequenceActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ABindSequenceActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

