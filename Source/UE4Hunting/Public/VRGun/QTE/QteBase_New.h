#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QteBase_New.generated.h"

USTRUCT(BlueprintType)
struct FQteInitData
{
	GENERATED_BODY()

	UPROPERTY() float Duration = 5.0f;
	UPROPERTY() float MaxHp = 100.0f;
	UPROPERTY() int32 QTELevel = 1;
	UPROPERTY() float Scale = 1.0f;
};

UCLASS()
class UE4HUNTING_API AQteBase_New : public AActor
{
	GENERATED_BODY()

public:
	AQteBase_New();

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaTime) override;

#pragma region /* 蓝图通信接口 (BIE) */
public:
	// 🌟 核心缓存：保存强转后的 UI 指针
	UPROPERTY(Transient)
	class UVRQteWidget* WBP_QTE = nullptr;

	// 0. 绑定 UI 指针
	void BindUI();

	// 1. 初始化 UI (合并 Property 和 Anim)
	void UpdateInitUI(float InSecond, int32 InLevel);

	// 2. 更新血条 UI
	void UpdateDamagedUI(float HpPercent);

	// 3. 死亡表现 UI
	void UpdateDieUI(int32 State);

	// 4. 重置状态 UI
	void UpdateResetStateUI();

	// 5. 获取颜色
	int32 GetUINColor();

#pragma endregion

#pragma region /* 核心函数 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Functions")
	virtual void InitProperty(float InMaxHp, float InDurationTime, int32 InQTELevel, float InActorScale);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "QTE|Functions")
	virtual float GetHitFxScale(USceneComponent* Target);

	UFUNCTION(BlueprintCallable, Category = "QTE|Functions")
	virtual void ResetState(int32 State);

	UFUNCTION(BlueprintCallable, Category = "QTE|Functions")
	void UpdateFxColor();

	void OnDead(bool IsDead);

	// 恢复满血
	void Revive(int InQTELevel);

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
#pragma endregion

#pragma region /* 网络同步 RPC 函数 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//UFUNCTION()
	//void OnRep_HP(float OldHP);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "QTE|Functions")
	virtual void Server_InitQTE();

    UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "QTE|Functions")
	void Multicast_Die(bool bAuto, int32 InQTELevel);
	
	// 普通受击掉血多播
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_TakeDamage(float CurrentHP);

	// 同步广播UI生命周期
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartQTETimer();

#pragma endregion

#pragma region /* 变量与组件 */
	UPROPERTY(ReplicatedUsing = OnRep_QteInitData)
	FQteInitData InitData;

	UFUNCTION()
	void OnRep_QteInitData();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float MaxHp = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float QteDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	int32 DamageIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	int32 QTELevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float QteFxScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "QTE|Variables")
	bool bIsDead = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float HP = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Audio")
	class USoundBase* ShatterSound = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QTE|Components")
	USceneComponent* RootComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QTE|Components")
	class UWidgetComponent* Widget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QTE|Components")
	class UNiagaraComponent* Qtefx_3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QTE|Components")
	class UNiagaraComponent* Qtefx;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QTE|Components")
	class USphereComponent* Sphere;
#pragma endregion
};