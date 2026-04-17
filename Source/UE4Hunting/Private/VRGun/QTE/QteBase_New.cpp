#include "VRGun/QTE/QteBase_New.h"
#include "Components/WidgetComponent.h"
#include "NiagaraComponent.h" 
#include "Components/SphereComponent.h"
#include "VRGun/GameModeBase/VRGunGameModeBase.h"
#include "VRGun/Enemy/EnemyBase_New.h"
#include <Kismet/GameplayStatics.h>
#include "Kismet/KismetMathLibrary.h"
#include "Sound/SoundBase.h"
#include "VRGun/Widget/VRQteWidget.h"
#include "Net/UnrealNetwork.h" 
#include "VRGun/VR_Controller/VR_PlayerController.h"

AQteBase_New::AQteBase_New()
{
	// 允许每帧执行 Tick（用于让 UI 面朝玩家）
	PrimaryActorTick.bCanEverTick = true;

	// ==========================================
	// 1. 组件装配
	// ==========================================
	RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(RootComp);

	Widget = CreateDefaultSubobject<UWidgetComponent>(TEXT("Widget"));
	Widget->SetupAttachment(RootComp);

	Qtefx_3 = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Qtefx_3"));
	Qtefx_3->SetupAttachment(Widget);

	Qtefx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Qtefx"));
	Qtefx->SetupAttachment(Widget);

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->SetupAttachment(RootComp);

	// ==========================================
	// 2. 碰撞设置 (核心：让武器的射线能打到它)
	// ==========================================
	Sphere->InitSphereRadius(40.0f);

	// 仅用于射线/重叠检测，不参与物理引擎的碰撞模拟（不会掉到地上）
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// 伪装成“敌人”通道，武器的 ObjectQueryParams 才能扫到它
	Sphere->SetCollisionObjectType(ECC_GameTraceChannel1);

	// 默认忽略所有，防止阻挡玩家走路
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 允许被武器射线 (Enemy 通道) 和 基础射线 (Visibility 通道) 击中
	Sphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// ==========================================
	// 3. 网络设置
	// ==========================================
	bReplicates = true;          // 开启 Actor 的网络同步
}

void AQteBase_New::BeginPlay()
{
	Super::BeginPlay();

	// 注册到 GameMode
	AVRGunGameModeBase* GM = Cast<AVRGunGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (GM) GM->RegisterQTE(this);
}

void AQteBase_New::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 恢复你原先的广告牌效果 (Billboard)：让 QTE 永远面朝 VR 玩家的头显
	if (IsValid(Widget))
	{
		APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
		if (CameraManager)
		{
			FVector StartLoc = Widget->GetComponentLocation();
			FVector TargetLoc = CameraManager->GetCameraLocation();

			// 计算从 UI 指向摄像机的旋转角度
			FRotator NewRotation = UKismetMathLibrary::FindLookAtRotation(StartLoc, TargetLoc);
			Widget->SetWorldRotation(NewRotation);
		}
	}
}

void AQteBase_New::Destroyed()
{
	if (!bIsDead)
	{
		OnDead(false);
	}
	
	Super::Destroyed();
}

void AQteBase_New::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AQteBase_New, bIsDead);
	DOREPLIFETIME(AQteBase_New, InitData);
}

void AQteBase_New::BindUI()
{
	// 如果 UI 还没绑定，且 3D UI 组件存在
	if (!WBP_QTE && Widget)
	{
		// 🌟 核心修复：如果是客户端早产导致 UI 还没生成，强行催产！
		if (!Widget->GetUserWidgetObject())
		{
			Widget->InitWidget(); // 强制组件立刻实例化内部的 UMG
		}

		// 现在再去拿，绝对能拿到了
		UUserWidget* UserWidget = Widget->GetUserWidgetObject();
		if (UserWidget)
		{
			// 强转为你自己写的 C++ UI 类
			WBP_QTE = Cast<UVRQteWidget>(UserWidget);
		}
		else
		{
			// 🚨 终极防御：如果催产了还是拿不到，说明你的 WidgetComponent 细节面板里，压根没设置 Widget Class！
			UE_LOG(LogTemp, Error, TEXT("[%s][%s] BindUI 失败：组件里没有配置 UI 类！"), HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName());
		}
	}
}

void AQteBase_New::UpdateInitUI(float InSecond, int32 InLevel)
{
	// 🌟 【日志】：记录是哪一端、哪个 QTE 在尝试初始化 UI，以及传进来的精准补偿时间
	UE_LOG(LogTemp, Warning, TEXT("[%s][%s] UpdateInitUI: 准备初始化 UI! 传入剩余时间: %.3f 秒, 传入等级: %d"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName(), InSecond, InLevel);

	if (WBP_QTE)
	{
		// 直接连招调用 C++ UI 函数
		WBP_QTE->InitProperty(InSecond, InLevel);
		WBP_QTE->InitAnim();

		// 🌟 【日志】：确认执行成功
		UE_LOG(LogTemp, Log, TEXT("[%s][%s] UpdateInitUI: ✅ UI 初始化连招调用成功！"),
			HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName());
	}
	else
	{
		// 🚨 【报错日志】：如果走到这里，说明 C++ 没拿到 UI 的指针，立刻标红警告！
		UE_LOG(LogTemp, Error, TEXT("[%s][%s] UpdateInitUI: ❌ 致命错误！WBP_QTE 指针为空，UI 未绑定或转换失败！"),
			HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName());
	}
}

void AQteBase_New::UpdateDamagedUI(float HpPercent)
{
	if (WBP_QTE)
	{
		WBP_QTE->OnDamaged(HpPercent);
	}
}

void AQteBase_New::UpdateDieUI(int32 State)
{
	if (WBP_QTE)
	{
		WBP_QTE->OnDie(State);
	}
}

void AQteBase_New::UpdateResetStateUI()
{
	if (WBP_QTE)
	{
		WBP_QTE->ToWhite();
		WBP_QTE->Level--;
	}
}

int32 AQteBase_New::GetUINColor()
{
	if(WBP_QTE)
	{
		return WBP_QTE->NColor;
	}

	return 0;
}

void AQteBase_New::InitProperty(float InMaxHp, float InDurationTime, int32 InQTELevel, float InActorScale)
{
	MaxHp = InMaxHp;
	if (HasAuthority())
	{
		HP = MaxHp;
	}

	bIsDead = false;
	QTELevel = InQTELevel;

	if (HasAuthority()) SetLifeSpan(InDurationTime);

	QteFxScale = InActorScale * 2.0f;
}

void AQteBase_New::Multicast_StartQTETimer_Implementation()
{
	UpdateInitUI(InitData.Duration, InitData.QTELevel);
}

void AQteBase_New::OnRep_QteInitData()
{
	// 0. 绑定UI
	BindUI();

	// 1. 把包裹里的数据赋值给本地变量（取代你原来的 InitProperty）
	InitProperty(InitData.MaxHp, InitData.Duration, InitData.QTELevel, InitData.Scale);

	if (Qtefx) Qtefx->SetVariableFloat(FName("Scale"), QteFxScale);

	Tags.AddUnique(FName("QTE"));
	Tags.AddUnique(FName("AllowDamage"));

	if (!HasAuthority())
	{
		APlayerController* LocalPC = UGameplayStatics::GetPlayerController(this, 0);
		AVR_PlayerController* MyPC = Cast<AVR_PlayerController>(LocalPC);

		if (MyPC)
		{
			MyPC->Server_ReportQTEReady(this);
		}
	}
}

// 初始化QTE
void AQteBase_New::Server_InitQTE_Implementation()
{
	if (HasAuthority())
	{
		// 服务器不需要等网络传输，直接手动调一次 OnRep 逻辑
		OnRep_QteInitData();
	}
}

void AQteBase_New::Multicast_TakeDamage_Implementation(float CurrentHP)
{
	if (bIsDead) return;

	UpdateFxColor();

	if (Qtefx_3)
	{
		Qtefx_3->SetVariableFloat(FName("Scale"), GetHitFxScale(Qtefx_3));
		Qtefx_3->Activate(true);
	}

	// 调用蓝图更新血条
	float HpPercent = (MaxHp > 0.0f) ? (CurrentHP / MaxHp) : 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[%s][%s] Multicast_TakeDamage: 🔫 播放普通受击！收到最新血量: %.1f, UI进度条更新为: %.2f"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName(), CurrentHP, HpPercent);

	UpdateDamagedUI(HpPercent);
}

// 死亡通知 UI
void AQteBase_New::Multicast_Die_Implementation(bool bAuto, int32 InQTELevel)
{
	UE_LOG(LogTemp, Log, TEXT("[%s][%s] Multicast_Die: 收到死亡/破盾多播！是否自动销毁: %d, 传入等级: %d"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName(), bAuto, InQTELevel);

	if (bAuto)
	{
		if (HasAuthority()) Destroy();
		else SetActorHiddenInGame(true);
	}
	else
	{
		if (ShatterSound) UGameplayStatics::PlaySound2D(this, ShatterSound);

		// 调用蓝图播放死亡/碎裂动画
		UpdateDieUI(InQTELevel);
		ResetState(InQTELevel);
	}
}

void AQteBase_New::UpdateFxColor()
{
	int32 NColor = GetUINColor(); // 索要蓝图颜色状态
	UE_LOG(LogTemp, Warning, TEXT("NColor = %d"), NColor);
	if (!Qtefx || !Qtefx_3) return;

	if (NColor == 1)
	{
		Qtefx->SetColorParameter(FName("Color"), FLinearColor(0.467366f, 1.0f, 0.0f, 1.0f));
		Qtefx_3->SetFloatParameter(FName("Hue"), -0.3f);
	}
	else if (NColor == 2)
	{
		Qtefx->SetColorParameter(FName("Color"), FLinearColor(1.0f, 0.115454f, 0.0f, 1.0f));
		Qtefx_3->SetFloatParameter(FName("Hue"), 0.45f);
	}
}

void AQteBase_New::OnDead(bool IsDead)
{
	// 汇报给老板 (由于角色没血条，不需要传 DamageCauser)
	AEnemyBase_New* Enemy = Cast<AEnemyBase_New>(GetOwner());
	if (Enemy)
	{
		Enemy->OnQteDestroyed(IsDead, this);
	}

	// 从 GameMode 清理自己
	AVRGunGameModeBase* GM = Cast<AVRGunGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (GM)
	{
		GM->UnregisterQTE(this);
	}
}

void AQteBase_New::Revive(int InQTELevel)
{
	UE_LOG(LogTemp, Warning, TEXT("[%s][%s] Revive ♻️: 触发复活表现！当前等级: %d, 正强制将 UI 血条压至 0.0 制造打击感！"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName(), InQTELevel);

	// 调用蓝图 UI 重置
	UpdateResetStateUI();

	UpdateFxColor();

	if (Qtefx_3)
	{
		Qtefx_3->SetVariableFloat(FName("Scale"), GetHitFxScale(Qtefx_3));
		Qtefx_3->Activate(true);
	}

	UpdateDamagedUI(1.0f);
}

float AQteBase_New::GetHitFxScale(USceneComponent* Target)
{
	return Target ? Target->GetComponentScale().GetMax() : 1.0f;
}

// 重置通知 UI
void AQteBase_New::ResetState(int32 InQTELevel)
{
	UE_LOG(LogTemp, Log, TEXT("[%s][%s] ResetState: 开始重置状态，目标等级: %d"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetName(), InQTELevel);

	float FxSt = (InQTELevel == 0) ? 500.0f : 200.0f;
	int32 FxCount = (InQTELevel == 0) ? 30 : 20;

	if (Qtefx)
	{
		Qtefx->SetVariableFloat(FName("St"), FxSt);
		Qtefx->SetVariableInt(FName("Count"), FxCount);
		Qtefx->SetVariableFloat(FName("Scale"), GetHitFxScale(Qtefx));
		Qtefx->Activate(true);
	}

	if (InQTELevel != 0)
	{
		// 恢复满血
		Revive(InQTELevel);
	}
	else
	{
		if (Sphere) Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OnDead(true);
		SetLifeSpan(0.55f);
	}
}

float AQteBase_New::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || bIsDead) return 0.0f;

	float OldHP = HP;
	HP--;

	UE_LOG(LogTemp, Warning, TEXT("[Server][%s] TakeDamage: 受击！扣血前: %.1f, 扣血后: %.1f, 当前 QTE 等级: %d"), *GetName(), OldHP, HP, QTELevel);

	Multicast_TakeDamage(HP);
	// OnRep_HP(OldHP);

	if (HP <= 0.0f)
	{
		QTELevel = FMath::Clamp(QTELevel - 1, 0, 2);
		if (QTELevel > 0)
		{
			HP = MaxHp;													// 只有服务器有资格满血
			UE_LOG(LogTemp, Warning, TEXT("[Server][%s] TakeDamage: 🛡️ 破盾！剩余等级降为: %d, 服务器底层血量已回满至: %.1f"), *GetName(), QTELevel, MaxHp);
			Multicast_Die(false, QTELevel);								// 触发多播：蓝图播破盾动画
		}
		else
		{
			bIsDead = true;												// 只有真正层数归零了
			UE_LOG(LogTemp, Warning, TEXT("[Server][%s] TakeDamage: 💀 彻底死亡！等级归零，准备销毁！"), *GetName());
			Multicast_Die(false, 0);									// 触发多播：蓝图播死亡销毁动画
		}
	}

	return ActualDamage;
}