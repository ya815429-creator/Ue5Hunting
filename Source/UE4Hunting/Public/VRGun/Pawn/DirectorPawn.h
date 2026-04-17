// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VRGun/Interface/IBindableEntity.h"
#include "Components/TimelineComponent.h"
#include "DirectorPawn.generated.h"

class AVRCharacter_Gun;
class UProSceneCaptureComponent2D;
class UTextureRenderTarget2D;
UCLASS()
class UE4HUNTING_API ADirectorPawn : public APawn, public IIBindableEntity
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ADirectorPawn();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
public:
    // ==========================================
    // 核心组件
    // ==========================================
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director")
    class USceneComponent* RootComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director")
    class UCameraComponent* DirectorCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director")
    class USequenceSyncComponent* SyncComp;

    /** 导播专用的场景捕获相机*/
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director")
    UProSceneCaptureComponent2D* CaptureComponent;

    /** 捕获画面的输出目标*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director")
    UTextureRenderTarget2D* DirectorRenderTarget;

   
    // ==========================================
    // 定序器绑定接口实现
    // ==========================================
    /** 默认绑定的轨道名称，去定序器里建一个叫 DirectorCamera 的轨道即可 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director")
    FName MyBindingTag = TEXT("DirectorCamera");

    virtual FName GetBindingTag() const override { return MyBindingTag; }
    virtual USequenceSyncComponent* GetSequenceSyncComponent() const override { return SyncComp; }

    // 隐藏特定 Actor (用于隐藏 1P 的 3D 准星)
    UFUNCTION(BlueprintCallable, Category = "Director")
    void HideActorFromDirector(AActor* ActorToHide);

public:
    FTimeline FadeTimeline;

    /*黑屏曲线*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director|Fade")
    class UCurveFloat* FadeCurve;

    UFUNCTION()
    void StartFadeProgress(float Value);

    UFUNCTION(BlueprintCallable)
    void PlayFadeTimeline(float LifeTime);

    UFUNCTION(BlueprintCallable)
    void ReverseFadeTimeline(float LifeTime);
};
