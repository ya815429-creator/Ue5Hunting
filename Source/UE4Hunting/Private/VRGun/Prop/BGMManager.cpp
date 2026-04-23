// Fill out your copyright notice in the Description page of Project Settings.


#include "VRGun/Prop/BGMManager.h"
#include "Net/UnrealNetwork.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "VRGun/GameInstance/VR_GameInstance.h"
#include "VRGun/VRCharacter_Gun.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
// Sets default values
ABGMManager::ABGMManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

    bReplicates = true;
    bAlwaysRelevant = true; // 确保导播和玩家永远能同步到 BGM 状态

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComp"));

    AudioChannelA = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioChannelA"));
    AudioChannelA->SetupAttachment(RootComponent);
    AudioChannelA->bAllowSpatialization = false;
    AudioChannelA->bAutoActivate = false;

    AudioChannelB = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioChannelB"));
    AudioChannelB->SetupAttachment(RootComponent);
    AudioChannelB->bAllowSpatialization = false;
    AudioChannelB->bAutoActivate = false;
}

// Called when the game starts or when spawned
void ABGMManager::BeginPlay()
{
	Super::BeginPlay();
	
}

void ABGMManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABGMManager, CurrentBGMTag);
}

void ABGMManager::SwitchBGM(FName BGMTag)
{
    if (HasAuthority() && CurrentBGMTag != BGMTag)
    {
        CurrentBGMTag = BGMTag;

        // 如果是 Listen Server (房主自己也玩或者兼任导播)，手动触发一次本地更新
        if (GetNetMode() != NM_DedicatedServer)
        {
            OnRep_CurrentBGMTag();
        }
    }
}

void ABGMManager::StopBGM()
{
    SwitchBGM(NAME_None); // 将标签设为空，代表停止
}

void ABGMManager::OnRep_CurrentBGMTag()
{
    // 如果标签是 None，说明服务器要求停止音乐
    if (CurrentBGMTag == NAME_None)
    {
        if (AudioChannelA->IsPlaying()) AudioChannelA->FadeOut(CrossfadeDuration, 0.0f);
        if (AudioChannelB->IsPlaying()) AudioChannelB->FadeOut(CrossfadeDuration, 0.0f);
        return;
    }

    // 1. 获取本地玩家的投币状态
    AVRGunGameStateBase* GS = GetWorld()->GetGameState<AVRGunGameStateBase>();
    AVRCharacter_Gun* LocalChar = Cast<AVRCharacter_Gun>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));

    FName FinalTag = CurrentBGMTag;

    if (GS && LocalChar)
    {
        bool bPaid = (LocalChar->AssignedPlayerIndex == 0) ? GS->Rep_P0_IsPaid : GS->Rep_P1_IsPaid;

        // 2. 如果是游戏内地图 BGM，且玩家没投币，则强行切换到“等待室”标签
        if (!bPaid && CurrentBGMTag != TEXT("Lobby"))
        {
            FinalTag = TEXT("WaitingRoomBGM"); // 这里需要在歌单 TMap 里配置这个标签
        }
    }

    // 查字典：去歌单里找这首歌
    if (!BGMPlaylist.Contains(FinalTag))
    {
        UE_LOG(LogTemp, Warning, TEXT("[BGM] 找不到名为 %s 的音乐！请检查蓝图歌单配置。"), *FinalTag.ToString());
        return;
    }

    USoundBase* NewBGM = BGMPlaylist[FinalTag];
    if (!NewBGM) return;

    // 核心交叉淡化逻辑
    UAudioComponent* ActiveChannel = bIsChannelAPlaying ? AudioChannelA : AudioChannelB;
    UAudioComponent* InactiveChannel = bIsChannelAPlaying ? AudioChannelB : AudioChannelA;

    // 旧音乐淡出
    if (ActiveChannel->IsPlaying())
    {
        ActiveChannel->FadeOut(CrossfadeDuration, 0.0f);
    }

    // 新音乐淡入
    InactiveChannel->SetSound(NewBGM);
    InactiveChannel->FadeIn(CrossfadeDuration, 1.0f);

    // 交换身份
    bIsChannelAPlaying = !bIsChannelAPlaying;

    UE_LOG(LogTemp, Log, TEXT("[BGM] 本地已切换至新背景音乐: %s"), *CurrentBGMTag.ToString());
}

