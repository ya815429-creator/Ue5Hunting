// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Pawn/DirectorPawn.h"
#include "Camera/CameraComponent.h"
#include "VRGun/Component/SequenceSyncComponent.h"
#include "VRGun/VRCharacter_Gun.h"
#include "Kismet/GameplayStatics.h"
#include "ProSceneCaptureComponent2D.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"

// Sets default values
ADirectorPawn::ADirectorPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = false;

    RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComp"));
    RootComponent = RootComp;

    DirectorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("DirectorCamera"));
    DirectorCamera->SetupAttachment(RootComp);

    SyncComp = CreateDefaultSubobject<USequenceSyncComponent>(TEXT("SequenceSync"));

    // 🌟 2. 实例化你自己的 Pro 捕获相机！
    CaptureComponent = CreateDefaultSubobject<UProSceneCaptureComponent2D>(TEXT("CaptureComponent"));
    CaptureComponent->SetupAttachment(DirectorCamera);

}

// Called when the game starts or when spawned
void ADirectorPawn::BeginPlay()
{
	Super::BeginPlay();
    if (HasAuthority() && DirectorRenderTarget)
    {
        CaptureComponent->TextureTarget = DirectorRenderTarget;

        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(DirectorRenderTarget);
        UE_LOG(LogTemp, Warning, TEXT("[Director] 旁观者屏幕已接管！Pro 捕获相机正在输出画面！"));
    }
    if (FadeCurve)
    {
        FOnTimelineFloat FadeProgress;
        FadeProgress.BindUFunction(this, FName("StartFadeProgress"));
        FadeTimeline.AddInterpFloat(FadeCurve, FadeProgress);
    }
}

void ADirectorPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    FadeTimeline.TickTimeline(DeltaTime);
}

void ADirectorPawn::HideActorFromDirector(AActor* ActorToHide)
{
    if (CaptureComponent && ActorToHide)
    {
        CaptureComponent->HiddenActors.AddUnique(ActorToHide);
    }
}

void ADirectorPawn::StartFadeProgress(float Value)
{
    if (CaptureComponent)
    {
        FVector4 ColorGain = FVector4(Value,Value,Value,1.0f);
        DirectorCamera->PostProcessSettings.ColorGain = ColorGain;
    }
}

void ADirectorPawn::PlayFadeTimeline(float LifeTime)
{
    float PlayRate = 1.0f / LifeTime;
    FadeTimeline.SetPlayRate(PlayRate);
    FadeTimeline.Play();
}

void ADirectorPawn::ReverseFadeTimeline(float LifeTime)
{
    float PlayRate = 1.0f / LifeTime;
    FadeTimeline.SetPlayRate(PlayRate);
    FadeTimeline.Reverse();
}
