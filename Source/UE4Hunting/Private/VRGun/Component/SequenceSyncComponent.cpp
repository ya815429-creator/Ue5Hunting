#include "VRGun/Component/SequenceSyncComponent.h"
#include "Net/UnrealNetwork.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "VRGun/GameStateBase/VRGunGameStateBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VRGun/Interface/IBindableEntity.h"

/*
  USequenceSyncComponent
  ------------------------------------------------------------
  组件职责（高层说明）：
  - 管理角色与 Level Sequence 的绑定与解绑（本地绑定与服务器同步标志）
  - 在合适的时刻触发 Sequence 播放（基于服务器统一的开始时间）
  - 在客户端发生严重卡顿时，基于服务器位置做弹性纠偏（避免小幅差异干扰本地演出）
  - 通过网络复制 BindingTag 与 bIsBound，使服务器可以控制绑定状态，并让客户端据此进行本地绑定/解绑
  - 提供对动态生成（中途刷出）的实体的兜底绑定：在 BeginPlay 中尝试从 GameState 获取并绑定本地序列

  主要成员变量用途：
  - TargetSeqActor: 当前目标的 LevelSequenceActor（可能为 nullptr）
  - BindingTag: 要与 Sequence 绑定的标签（为 None 表示未指定）
  - GlobalStartTime: 全局（服务器）规定的序列开始时间（用于统一所有客户端的发车）
  - bHasLocallyBound: 标记本地是否已执行过 SetBinding（避免重复绑定）
  - bIsBound: 网络复制的标志，表示服务器认为该实体已绑定（服务器端 authoritative）
  - bHasTriggeredPlay: 本地是否已触发 Player->Play（防止重复 Play）
*/

USequenceSyncComponent::USequenceSyncComponent()
{
	// 启用主组件的每一帧更新功能，用于在 Tick 中处理播放触发与纠偏逻辑
	PrimaryComponentTick.bCanEverTick = true;

	// 默认使组件可复制（必要时可在 Actor 上控制）
	SetIsReplicatedByDefault(true);
}

void USequenceSyncComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 复制 BindingTag：当服务器改变绑定标签时，客户端可收到通知并尝试绑定
	DOREPLIFETIME(USequenceSyncComponent, BindingTag);

	// 复制 bIsBound：服务器控制是否认为实体已被绑定，客户端收到后会执行绑定或解绑
	DOREPLIFETIME(USequenceSyncComponent, bIsBound);
}

void USequenceSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	// 兜底机制：处理那些中途动态生成（在序列开始后生成）的实体
	// 这些实体在 BeginPlay 时尝试从 GameState 获取当前本地序列并进行绑定，以保证中途刷出的实体也能参与序列
	if (AVRGunGameStateBase* GS = GetWorld()->GetGameState<AVRGunGameStateBase>())
	{
		if (GS->LocalSequenceActor)
		{
			// 只有实现 IBindableEntity 接口的 Actor 才能被绑定；从实体读取绑定用的 Tag
			if (IIBindableEntity* Ent = Cast<IIBindableEntity>(GetOwner()))
			{
				// 将 GameState 的本地序列、服务器的活动开始时间与实体的绑定 Tag 一并传入进行初始化与绑定尝试
				LocalInitAndBind(GS->LocalSequenceActor, GS->ActiveSequenceStartTime, Ent->GetBindingTag());
			}
		}
	}
}

/*
  LocalInitAndBind
  功能：
  - 本地维护 TargetSeqActor/GlobalStartTime/BindingTag，并尝试执行绑定与寻位（ApplyBindingAndSeek）
  - 修复点：如果传入的 SequenceActor 与当前不同，需要重置本地绑定锁与 Play 触发标志，
    否则将拒绝绑定到新的定序器（因为上一次的 bHasLocallyBound 阻止重新绑定）
*/
void USequenceSyncComponent::LocalInitAndBind(ALevelSequenceActor* InSeq, float InStartTime, FName InTag)
{
	// ==========================================
	// 修复 2：当定序器发生替换时（旧的被销毁并重建），必须清掉本地绑定/播放状态
	// 否则组件会认为已经绑定在旧的定序器上而拒绝对新定序器绑定
	// ==========================================
	if (TargetSeqActor != InSeq)
	{
		bHasLocallyBound = false;
		bHasTriggeredPlay = false;
	}

	TargetSeqActor = InSeq;
	GlobalStartTime = InStartTime;

	// 如果传入的 Tag 有效，则覆盖当前 BindingTag
	if (!InTag.IsNone()) BindingTag = InTag;

	// 尝试真正的绑定与寻位（此函数内部会检测权限与先决条件）
	ApplyBindingAndSeek();
}

/*
  OnRep_BindingTag
  说明：
  - 当 BindingTag 在网络上发生变化时（Replication Notify），客户端/服务端都会触发该函数
  - 只要 Tag 发生改变，就需要重置本地绑定锁并尝试重新绑定（因为 Tag 变更意味着绑定目标发生变化）
*/
void USequenceSyncComponent::OnRep_BindingTag()
{
	// 只要 Tag 发生了网络改变，立刻重置本地锁，允许重新绑定
	bHasLocallyBound = false;
	ApplyBindingAndSeek();
}

/*
  ApplyBindingAndSeek
  说明（关键流程）：
  1. 如果已经本地绑定过，直接返回（避免重复 SetBinding）
  2. 如果当前实体既不是服务器拥有者（Authority），又未被服务器标记为绑定（bIsBound == false），则返回
     -> 这一步保证客户端不会在未被服务器批准时自行绑定（服务器可通过 bIsBound 控制）
  3. 确定要使用的 BindingTag（优先使用组件自身的 BindingTag，否则尝试从所属实体的 IBindableEntity 接口获取）
  4. 如果没有目标 Sequence 或 Tag，无效则返回
  5. 为保证同步表现一致，关闭 Actor 移动复制并停掉 CharacterMovement（防止物理运动影响 Sequence 演出）
  6. 强制提前创建 SequencePlayer（修复点 3：部分情况延迟创建会导致绑定失败）
  7. 调用 SetBindingByTag 将当前 Actor 绑定到 Sequence（使用 Tag）
  8. 更新本地状态标志（bHasLocallyBound, bHasTriggeredPlay）并在服务器端把 bIsBound 标记为 true（用于复制）
*/
void USequenceSyncComponent::ApplyBindingAndSeek()
{
	// 已经本地绑定过则无需重复绑定
	if (bHasLocallyBound) return;

	// 如果不是服务器且服务器没有标记为绑定（bIsBound），客户端不应擅自绑定
	if (!GetOwner()->HasAuthority() && !bIsBound) return;

	// 决定要使用的 Tag：优先使用组件记录的 BindingTag，否则从实体接口中获取
	FName TagToUse = BindingTag;
	if (TagToUse.IsNone())
	{
		if (IIBindableEntity* Ent = Cast<IIBindableEntity>(GetOwner()))
			TagToUse = Ent->GetBindingTag();
	}

	// 无目标定序器或没有有效 Tag 时，不执行绑定
	if (!TargetSeqActor || TagToUse.IsNone()) return;

	// 将最终 Tag 写回组件状态（以便后续复制与查询一致）
	BindingTag = TagToUse;

	// 在绑定期间关闭位移复制与角色移动，确保 Sequence 播放期间画面稳定并由 Sequence 控制动画/位移
	GetOwner()->SetReplicateMovement(false);
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		OwnerChar->GetCharacterMovement()->DisableMovement();
		OwnerChar->GetCharacterMovement()->StopMovementImmediately();
	}

	// ==========================================
	// 修复 3：有时底层不会即时创建 SequencePlayer，必须提前获取并确保创建完成
	// 否则后续 SetBinding 可能失效或行为异常
	// ==========================================
	TargetSeqActor->GetSequencePlayer();

	// 执行绑定：将当前 Actor 通过 Tag 绑定到目标 SequenceActor
	TargetSeqActor->SetBindingByTag(BindingTag, TArray<AActor*>{GetOwner()});

	// 标记本地已经绑定成功，等待播放触发（bHasTriggeredPlay 控制 Play 的触发一次性）
	bHasLocallyBound = true;
	bHasTriggeredPlay = false; // 准备自主对表起跑！

	// 如果是服务器端拥有者，设置网络复制状态告知客户端（bIsBound 会被复制到客户端）
	if (GetOwner()->HasAuthority()) bIsBound = true;
}

/*
  UnbindFromSequence / OnRep_IsBound / PerformUnbind
  说明：
  - UnbindFromSequence: 供服务器调用以终止绑定，修改 bIsBound（仅在服务器生效）
  - OnRep_IsBound: 当 bIsBound 在客户端发生变化时触发，决定是绑定还是解绑
  - PerformUnbind: 真正执行解绑操作，恢复移动复制与角色移动 tick
*/
void USequenceSyncComponent::UnbindFromSequence()
{
	if (GetOwner()->HasAuthority())
	{
		if (!bIsBound) return;
		bIsBound = false;
		PerformUnbind();
	}
}

void USequenceSyncComponent::OnRep_IsBound()
{
	if (bIsBound) ApplyBindingAndSeek();
	else PerformUnbind();
}

void USequenceSyncComponent::PerformUnbind()
{
	// 如果本地根本没有绑定过，则无需执行解绑
	if (!bHasLocallyBound) return;

	// 清理本地状态
	bHasLocallyBound = false;
	bHasTriggeredPlay = false;

	// 如果目标 Sequence 和 Tag 有效，移除绑定关系
	if (TargetSeqActor && !BindingTag.IsNone())
	{
		TargetSeqActor->RemoveBindingByTag(BindingTag, GetOwner());
	}

	// 恢复 Actor 的移动复制与角色移动（使其回到正常游戏逻辑控制）
	GetOwner()->SetReplicateMovement(true);
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		// 恢复 CharacterMovement 的 tick（之前我们用 DisableMovement 停止了移动）
		OwnerChar->GetCharacterMovement()->SetComponentTickEnabled(true);
	}
}

/*
  TickComponent
  说明（每帧逻辑）：
  - 如果本地已绑定且存在目标 SequenceActor，获取 SequencePlayer 并进行以下处理：
    1. 发车倒计时：当服务器世界时间到达 GlobalStartTime 时触发 Player->Play（只触发一次）
    2. 弹性容差纠偏（仅客户端在播放中且非 Authority）：当本地播放时间与服务器记录的 ServerSequencePosition 差距过大时，强制跳转到服务器时间点
       - 只在差距超过阈值（0.5 秒）时才进行纠偏，阈值之内容忍并避免打断本地演出（能承受小幅慢帧）
       - 纠偏采用 Jump 更新方法，直接设置播放位置
*/
void USequenceSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 仅当本地已经绑定并且存在目标 SequenceActor 时才工作
	if (bHasLocallyBound && TargetSeqActor)
	{
		if (ULevelSequencePlayer* Player = TargetSeqActor->GetSequencePlayer())
		{
			// 1. 发车倒计时起跑逻辑（第一次达到服务器开始时间时触发 Play）
			if (!bHasTriggeredPlay)
			{
				// 使用 GameState 的服务器世界时间作为统一时钟来源
				float ServerTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
				if (ServerTime >= GlobalStartTime)
				{
					Player->Play();
					bHasTriggeredPlay = true;
				}
			}
			// ==========================================
			// 2. 弹性容差纠偏机制（防客户端大卡顿）
			//    - 只在 Player 正在播放且当前实例不是服务器 Authority 时执行
			//    - 通过 GameState->ServerSequencePosition 获取服务器上记录的序列位置
			//    - 当本地与服务器时间误差超过 0.5 秒时强制跳转（Jump）
			//    - 小于 0.5 秒时容忍差异，避免影响本地节奏
			// ==========================================
			else if (Player->IsPlaying() && !GetOwner()->HasAuthority())
			{
				AVRGunGameStateBase* GS = GetWorld()->GetGameState<AVRGunGameStateBase>();
				if (GS && GS->ServerSequencePosition > 0.0f)
				{
					float LocalSeqTime = Player->GetCurrentTime().AsSeconds();
					float ServerSeqTime = GS->ServerSequencePosition;
					float Drift = FMath::Abs(LocalSeqTime - ServerSeqTime);

					// 只有误差超过阈值（0.5s）时才强制纠偏
					if (Drift > 0.5f)
					{
						FMovieSceneSequencePlaybackParams Params;
						Params.Time = ServerSeqTime;
						Params.PositionType = EMovieScenePositionType::Time;
						Params.UpdateMethod = EUpdatePositionMethod::Jump;
						Player->SetPlaybackPosition(Params);

						UE_LOG(LogTemp, Warning, TEXT("检测到客户端大卡顿！已从 %.2f 强制纠偏至服务器进度 %.2f"), LocalSeqTime, ServerSeqTime);
					}
				}
			}
		}
	}
}