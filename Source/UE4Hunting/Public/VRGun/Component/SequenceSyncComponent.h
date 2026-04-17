#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SequenceSyncComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE4HUNTING_API USequenceSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USequenceSyncComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;
public:
	// 本地绑定接口
	void LocalInitAndBind(class ALevelSequenceActor* InSeq, float InStartTime, FName InTag);

	// 供外部查询绑定状态的接口
	UFUNCTION(BlueprintPure, Category = "Sequence")
	bool CheckIsBound() const { return bIsBound; }

	// 本地解绑接口
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Sequence")
	void UnbindFromSequence();

	// 服务器意图锁
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsBound, Category = "Sequence")
	bool bIsBound = false;

	// 本地执行锁
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bHasLocallyBound = false;

	// 本地倒计时起跑锁
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bHasTriggeredPlay = false;

	// RepNotify: 当客户端收到服务器下发的绑定状态更新时触发
	UFUNCTION()
	void OnRep_IsBound();

protected:
	// 内部执行移除绑定的函数
	void PerformUnbind();

public:
	// 以下全部转为纯本地指针与变量，不再受网络生硬干预！
	UPROPERTY(BlueprintReadOnly)
	class ALevelSequenceActor* TargetSeqActor;

	UPROPERTY(BlueprintReadOnly)
	float GlobalStartTime;

	UPROPERTY(ReplicatedUsing = OnRep_BindingTag)
	FName BindingTag;

	UFUNCTION()
	void OnRep_BindingTag();

	void ApplyBindingAndSeek();
};