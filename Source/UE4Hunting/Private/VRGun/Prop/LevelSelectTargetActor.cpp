// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Prop/LevelSelectTargetActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "VRGun/GameModeBase/VRLobbyGameMode.h"
#include "VRGun/Global/Enum.h"
#include "Components/SkeletalMeshComponent.h"
// Sets default values
ALevelSelectTargetActor::ALevelSelectTargetActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true; // 必须支持网络同步

    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    RootComponent = CollisionComp;
    CollisionComp->InitSphereRadius(50.0f);

    // 伪装成 Enemy 通道，这样你的假子弹射线就能精准打中它！
    CollisionComp->SetCollisionObjectType(COLLISION_ENEMY);
    CollisionComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionComp->SetCollisionResponseToChannel(COLLISION_ENEMY, ECR_Block);

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(RootComponent);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkelectMeshComp"));
    SkeletalMesh->SetupAttachment(RootComponent);
    SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // 贴上你的伤害判定标签
    Tags.AddUnique(FName("AllowDamage"));
}

// Called when the game starts or when spawned
void ALevelSelectTargetActor::BeginPlay()
{
	Super::BeginPlay();
	
}

float ALevelSelectTargetActor::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
    // 只有服务器有资格处理开枪选关
    if (!HasAuthority()) return 0.0f;

    // 播放靶子被击中的特效/音效 (这里保留你的 Multicast)
    Multicast_PlayHitEffect();

    // 获取当前地图的 GameMode
    if (AVRLobbyGameMode* LobbyGM = Cast<AVRLobbyGameMode>(GetWorld()->GetAuthGameMode()))
    {
        // 向 GameMode 提交一票！(把设定的关卡名和开枪者传过去)
        LobbyGM->SubmitVote(TargetLevelName, EventInstigator);
    }

    return DamageAmount;
}

void ALevelSelectTargetActor::Multicast_PlayHitEffect_Implementation()
{
    // 此时靶子绝对不会消失了！

    // 你可以在这里加入视觉反馈，让玩家知道自己打中了，例如：
    // 1. 播放一个受击音效
    // UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation());

    // 2. 播放一个粒子特效 (比如火花)
    // UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), HitParticles, GetActorLocation());

    // 3. (简单的临时反馈) 让模型稍微缩放一下抖动
    FVector CurrentScale = GetActorScale();
    SetActorScale3D(CurrentScale * 1.1f);
    // 用 Timer 在 0.1 秒后恢复原状，形成“挨打鼓起来一下”的视觉效果
    FTimerHandle ScaleTimer;
    GetWorld()->GetTimerManager().SetTimer(ScaleTimer, [this, CurrentScale]()
        {
            SetActorScale3D(CurrentScale);
        }, 0.1f, false);
}

