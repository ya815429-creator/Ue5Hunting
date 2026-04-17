// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Prop/VRDamageNumberActor.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Camera/PlayerCameraManager.h"
// Sets default values
AVRDamageNumberActor::AVRDamageNumberActor()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = false;

    TextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TextComponent"));
    RootComponent = TextComponent;

    TextComponent->SetHorizontalAlignment(EHTA_Center);
    TextComponent->SetVerticalAlignment(EVRTA_TextCenter);
    TextComponent->SetWorldSize(25.0f);
}

// Called when the game starts or when spawned
void AVRDamageNumberActor::BeginPlay()
{
    Super::BeginPlay();
    CachedCameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
    SetLifeSpan(MaxLifeTime); // 引擎底层控制：不管发生了什么，1秒后强行销毁
}

// Called every frame
void AVRDamageNumberActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. 物理运动与视线对齐
    AddActorWorldOffset(FVector(0.0f, 0.0f, FloatSpeed * DeltaTime));

    if (CachedCameraManager)
    {
        FVector CamLoc = CachedCameraManager->GetCameraLocation();
        FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(GetActorLocation(), CamLoc);

        // 【单次爆发摇摆特效】
        if (bIsShaking && CurrentLifeTime <= ShakeDuration && ShakeDuration > 0.0f)
        {
            float ShakePhase = (CurrentLifeTime / ShakeDuration) * PI * 2.0f;
            float ShakeAngle = FMath::Sin(ShakePhase) * ShakeMagnitude;
            LookAtRot.Roll += ShakeAngle;
        }

        SetActorRotation(LookAtRot);

        // 近大远小：恒定屏幕占比缩放
        float Distance = FVector::Distance(GetActorLocation(), CamLoc);
        float DynamicScale = Distance * 0.002f;
        DynamicScale = FMath::Clamp(DynamicScale, 0.5f, 4.0f);
        SetActorScale3D(FVector(DynamicScale));
    }

    // 2. 透明度渐变消失
    CurrentLifeTime += DeltaTime;
    float Alpha = FMath::Clamp(1.0f - (CurrentLifeTime / MaxLifeTime), 0.0f, 1.0f);

    FColor CurrentColor = BaseColor;
    CurrentColor.A = FMath::RoundToInt(Alpha * 255.0f);
    TextComponent->SetTextRenderColor(CurrentColor);
}

void AVRDamageNumberActor::InitDamageText(float InDamage, FVector HitLocation)
{
    int32 BaseDamage = FMath::RoundToInt(InDamage);

    // 1. 【颜色与暴击抽奖】
    float ColorRand = FMath::FRand();
    bool bIsCrit = false;

    if (ColorRand <= RedColorProb)
    {
        BaseColor = RedColor;
        bIsCrit = true;
    }
    else if (ColorRand <= (RedColorProb + YellowColorProb))
    {
        BaseColor = YellowColor;
        bIsCrit = true;
    }
    else
    {
        BaseColor = NormalColor;
        bIsCrit = false;
    }

    TextComponent->SetTextRenderColor(BaseColor);

    // 2. 【基于真实伤害动态增加的假数值生成】
    int32 FakeDamage = 0;
    if (bIsCrit)
    {
        // 暴击区间：基础伤害 + 暴击下限附加值 ~ 基础伤害 + 暴击上限附加值
        int32 CritMin = BaseDamage + CritExtraMin;
        int32 CritMax = FMath::Max(CritMin, BaseDamage + CritExtraMax); // 容错：防止蓝图里上限配得比下限还低
        FakeDamage = FMath::RandRange(CritMin, CritMax);
    }
    else
    {
        // 普通区间：基础伤害 ~ 基础伤害 + 普通上限附加值
        int32 NormalMax = FMath::Max(BaseDamage, BaseDamage + NormalExtraMax); // 容错处理
        FakeDamage = FMath::RandRange(BaseDamage, NormalMax);
    }

    // 定格显示最终生成的动态数字
    TextComponent->SetText(FText::FromString(FString::FromInt(FakeDamage)));

    // 3. 【摇摆抽奖】
    bIsShaking = FMath::FRand() < ShakeProbability;
    // 4. 防遮挡拉近与【完美的视角防重叠随机散布】
    if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        FVector CamLoc = Cam->GetCameraLocation();
        // DirToCam 是从命中点指向玩家摄像机的向量
        FVector DirToCam = (CamLoc - HitLocation).GetSafeNormal();

        // 1. 防穿模：向玩家方向强制拉近 30 厘米
        FVector OffsetLoc = HitLocation + (DirToCam * 30.0f);

        // 2. 基础抬高
        OffsetLoc.Z += InitialZOffset;

        // 3. 【左右随机】：利用“世界向上向量”与“朝向玩家向量”进行叉乘，
        // 得到一个永远垂直于玩家视线的“纯粹水平向右向量”！
        // 这样数字只会相对于玩家的眼睛在左右平行滑动，绝对不会发生前后重叠！
        FVector RightVector = FVector::CrossProduct(FVector::UpVector, DirToCam).GetSafeNormal();

        // 在 -RandomOffsetRightMax 到 +RandomOffsetRightMax 之间随机
        float RightRandom = FMath::RandRange(-RandomOffsetRightMax, RandomOffsetRightMax);
        OffsetLoc += (RightVector * RightRandom);

        // 4. 【向上随机】：从 0 到 RandomOffsetUpMax 随机，保证只有上没有下
        float UpRandom = FMath::RandRange(0.0f, RandomOffsetUpMax);
        OffsetLoc.Z += UpRandom;

        SetActorLocation(OffsetLoc);
    }
}

