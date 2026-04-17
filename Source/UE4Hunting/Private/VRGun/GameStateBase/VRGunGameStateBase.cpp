#include "VRGun/GameStateBase/VRGunGameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "Kismet/GameplayStatics.h"
#include "VRGun/Interface/IBindableEntity.h"
#include "VRGun/Component/SequenceSyncComponent.h"
#include "VRGun/GameInstance/VR_GameInstance.h"
#pragma region /* 生命周期与初始化 */

AVRGunGameStateBase::AVRGunGameStateBase()
{
	// 开启 GameState 的 Tick，用于服务器实时播报定序器的真实进度
	PrimaryActorTick.bCanEverTick = true;
}

/**
 * 注册全局网络同步变量
 */
void AVRGunGameStateBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVRGunGameStateBase, MatchStartCountdown);
	DOREPLIFETIME(AVRGunGameStateBase, ActiveSequenceAsset);
	DOREPLIFETIME(AVRGunGameStateBase, ActiveSequenceStartTime);
	DOREPLIFETIME(AVRGunGameStateBase, ServerSequencePosition); // 用于纠偏的同步灯塔
	DOREPLIFETIME(AVRGunGameStateBase, GlobalSequencePlayRate);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_bIsFreeMode);
	DOREPLIFETIME(AVRGunGameStateBase, GlobalCoinsPerCredit);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P0_Coins);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P0_Credits);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P0_IsPaid);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P1_Coins);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P1_Credits);
	DOREPLIFETIME(AVRGunGameStateBase, Rep_P1_IsPaid);
}

void AVRGunGameStateBase::BeginPlay()
{
	Super::BeginPlay();
	// 🌟 关卡刚加载时，主动去 GameInstance 把持久化的投币数据拉取过来同步！
	if (HasAuthority())
	{
		if (UVR_GameInstance* GI = Cast<UVR_GameInstance>(GetGameInstance()))
		{
			GI->SyncCoinDataToGameState(); // 让 GI 把数据推给自己
		}
	}
}

#pragma endregion


#pragma region /* 序列进度广播 */

/**
 * 每帧更新：服务器作为灯塔，向所有客户端广播当前的序列播放进度
 */
void AVRGunGameStateBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 服务器实时广播真实的定序器播放进度
	// 【核心架构】：该进度完美包含了可能存在的慢动作（TimeDilation）或卡顿影响，是全网唯一真实时间轴。
	if (HasAuthority() && LocalSequenceActor)
	{
		if (ULevelSequencePlayer* Player = LocalSequenceActor->GetSequencePlayer())
		{
			if (Player->IsPlaying())
			{
				ServerSequencePosition = Player->GetCurrentTime().AsSeconds();
			}
		}
	}
}

#pragma endregion


#pragma region /* 序列切换与绑定机制 */

/**
 * 切换关卡序列（仅限服务器权威调用）
 * 设定全网统一的起跑时间，并通过更新同步变量通知所有客户端。
 */
void AVRGunGameStateBase::Server_SwitchSequence(ULevelSequence* NewAsset)
{
	if (!HasAuthority() || !NewAsset) return;

	// 设定全场统一的未来发车时间（缓冲 0.5 秒，抵抗网络延迟）
	ActiveSequenceStartTime = GetServerWorldTimeSeconds() + 0.5f;
	ActiveSequenceAsset = NewAsset;

	// 服务器自身不会触发 RepNotify，因此需要手动调用一次本地生成逻辑
	OnRep_ActiveSequenceData();
}

/**
 * RepNotify 回调：全网生成纯本地定序器，并分发绑定指令
 * 当客户端/服务器收到新的 ActiveSequenceAsset 时执行。
 */
void AVRGunGameStateBase::OnRep_ActiveSequenceData()
{
	if (!ActiveSequenceAsset) return;

	// 1. 清理旧的本地序列实例
	if (LocalSequenceActor)
	{
		LocalSequenceActor->Destroy();
		LocalSequenceActor = nullptr;
	}

	// 2. 核心架构：纯本地生成！彻底切断 UE 底层生硬的网络位置同步
	FActorSpawnParameters SpawnParams;
	LocalSequenceActor = GetWorld()->SpawnActor<ALevelSequenceActor>(ALevelSequenceActor::StaticClass(), SpawnParams);
	LocalSequenceActor->SetSequence(ActiveSequenceAsset);

	// 【绝对禁止】：绝不调用 SetReplicates(true)！让它成为一个纯单机物体，各自在本地播放。

	// 3. 遍历当前场景中所有支持绑定的实体，通知它们绑定到这个新生成的本地序列上
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UIBindableEntity::StaticClass(), FoundActors);
	for (AActor* A : FoundActors)
	{
		if (IIBindableEntity* Interface = Cast<IIBindableEntity>(A))
		{
			if (USequenceSyncComponent* SyncComp = Interface->GetSequenceSyncComponent())
			{
				// 将本地序列实例、统一的起跑时间、以及实体的专属 Tag 下发给同步组件执行绑定
				SyncComp->LocalInitAndBind(LocalSequenceActor, ActiveSequenceStartTime, Interface->GetBindingTag());
			}
		}
	}
}

#pragma endregion

// 服务器端蓝图调用的函数
void AVRGunGameStateBase::SetGlobalSequencePlayRate(float NewRate)
{
	if (HasAuthority())
	{
		GlobalSequencePlayRate = NewRate; // 修改变量
		OnRep_GlobalSequencePlayRate();   // 服务器自己也要手动调一次表现
	}
}

// 客户端收到变量改变时自动执行
void AVRGunGameStateBase::OnRep_GlobalSequencePlayRate()
{
	if (LocalSequenceActor)
	{
		if (ULevelSequencePlayer* SeqPlayer = LocalSequenceActor->GetSequencePlayer())
		{
			SeqPlayer->SetPlayRate(GlobalSequencePlayRate);
			UE_LOG(LogTemp, Warning, TEXT("[GameState] 同步变量生效！全网速率已修改为 -> %f"), GlobalSequencePlayRate);
		}
	}
}

void AVRGunGameStateBase::Multicast_GlobalSequencePlayRate_Implementation(float NewRate)
{
	if (LocalSequenceActor)
	{
		if (ULevelSequencePlayer* SeqPlayer = LocalSequenceActor->GetSequencePlayer())
		{
			SeqPlayer->SetPlayRate(NewRate);
			UE_LOG(LogTemp, Warning, TEXT("[GameState] 多播变量生效！全网速率已修改为 -> %f"), GlobalSequencePlayRate);
		}
	}
}

void AVRGunGameStateBase::UpdateCoinData(bool bFreeMode, int32 Cost, int32 P0_Coins, int32 P0_Credits, bool P0_Paid, int32 P1_Coins, int32 P1_Credits, bool P1_Paid)
{
	if (!HasAuthority()) return;

	Rep_bIsFreeMode = bFreeMode;
	GlobalCoinsPerCredit = Cost;

	Rep_P0_Coins = P0_Coins;
	Rep_P0_Credits = P0_Credits;
	Rep_P0_IsPaid = P0_Paid;

	Rep_P1_Coins = P1_Coins;
	Rep_P1_Credits = P1_Credits;
	Rep_P1_IsPaid = P1_Paid;
}