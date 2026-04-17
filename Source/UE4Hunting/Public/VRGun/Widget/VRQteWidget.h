#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VRQteWidget.generated.h"

// 提前声明 UMG 组件类，防止报错
class UCanvasPanel;
class UImage;

/**
 * VR QTE UI 控件
 */
UCLASS()
class UE4HUNTING_API UVRQteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 初始化 QTE 属性
	 * @param InSecond 传入的倒计时秒数
	 * @param InLevel  传入的 QTE 等级
	 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Init")
	void InitProperty(float InSecond, int32 InLevel);

	/**
	 * 初始化并播放对应的 UI 动画
	 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Init")
	void InitAnim();

	/**
	 * QTE 受击/进度更新逻辑 (替代蓝图的 OnDamaged)
	 * @param InValue 传入的受击数值或进度，用于驱动材质的 Alpha 参数
	 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Damage")
	void OnDamaged(float InValue);

	/**
	 * QTE 结束/怪物死亡时的动画逻辑 (替代蓝图的 OnDie)
	 * @param State 传入的状态值，决定播放哪个消失动画 (0: Disappear, 1: Disa2, 2: Disa3)
	 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Animation")
	void OnDie(int32 State);

	/**
	 * QTE 成功时的变白/高亮动画逻辑 (替代蓝图的 ToWhite)
	 */
	UFUNCTION(BlueprintCallable, Category = "QTE|Animation")
	void ToWhite();
public:
	// ==========================================
	// 🌟 图片中的数值变量
	// ==========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float Second;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	int32 NColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	float Alpha;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QTE|Variables")
	int32 Level;

protected:
	// ==========================================
	// UMG 控件绑定 (必须在蓝图里有完全同名的控件！)
	// ==========================================

	// 画布面板 (Canvas Panels)
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UCanvasPanel* B1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UCanvasPanel* B2;

	// 图片控件 (Images)
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* C1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* Q1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* Q2;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* Q3;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* Q4;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UImage* Q5;

	// ==========================================
	// 🌟 UMG 动画绑定 (必须在蓝图里有完全同名的动画)
	// ==========================================

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Disa3;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Disa2;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Appear3;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Appear2;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Turn2;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Turn;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Disappear;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* ToWhiteAnim;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Damage;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetAnim))
	UWidgetAnimation* Appear;
};