#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/TimelineComponent.h"
#include "VRGun/Global/VRStruct.h"
#include "VRGun/Interface/IBindableEntity.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "EnemyBase_New.generated.h"

class AQteBase_New;


// 当一批 QTE 生成完成时触发的委托（无参数，仅作为信号）
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQteSpawnFinishedDelegate);

// 当整个 QTE 阶段结束时触发的委托
// @param bIsPhaseSuccessful  表示玩家是否成功完成该 QTE 阶段
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQtePhaseFinishedDelegate, bool, bIsPhaseSuccessful);


// AEnemyBase_New
// ----------------
// 怪物基类，负责：
//  - 从数据表初始化实例
//  - 关键配置的网络同步（索引 / 模型变体）
//  - 伤害与死亡逻辑（服务器权威）
//  - 死亡表现的多播（动画 / 物理 / 特效）
//  - QTE 的生成与生命周期管理
//  - 小怪召唤辅助功能
//
// 网络模型说明：
//  - 服务器对血量、死亡和数据驱动的初始化负责权威处理
//  - 使用 RepNotify 在客户端同步视觉和状态
UCLASS()
class UE4HUNTING_API AEnemyBase_New : public ACharacter, public IIBindableEntity
{
    GENERATED_BODY()

public:
    AEnemyBase_New();

#pragma region /* 生命周期与初始化 */
protected:
    // 引擎开始回调，服务器在此处执行数据初始化
    virtual void BeginPlay() override;

public:
    // 每帧 Tick（如启用）
    virtual void Tick(float DeltaTime) override;
    // 注册需要复制的属性
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // 从数据表初始化该敌人实例（在服务器上调用）
    UFUNCTION(BlueprintCallable, Category = "Enemy|Data")
    void InitEnemyData(int32 InEnemyIndex, int32 InEnemyMeshIndex);

protected:
    // 防止 BeginPlay 与 RepNotify 同时触发导致重复初始化
    bool bHasInitialized = false;

    // 当 EnemyIndex / EnemyMeshIndex 在客户端被复制后触发的回调
    UFUNCTION()
    void OnRep_EnemyData();
#pragma endregion


#pragma region /* 组件 */
public:
    // 与序列/定序器同步的组件
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    class USequenceSyncComponent* SyncComp;

    // 用于脚底阴影或局部贴花的 Decal 组件
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    class UDecalComponent* ShadowDecal;

    // 附加的静态网格（例如可破坏护甲或道具）
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    class UStaticMeshComponent* ExtraMesh;

    // 编辑器方向箭头 / 兜底特效生成点
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    class UArrowComponent* DirectionArrow;

    // 物理动画组件，用于骨骼层级的物理动画（布娃娃混合）
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    class UPhysicalAnimationComponent* PhysicalAnimation;

    // 运行时动态创建的 Niagara 组件列表，便于重复初始化时清理
    UPROPERTY(Transient)
    TArray<UNiagaraComponent*> DynamicNiagaraComps;
#pragma endregion


#pragma region /* 属性与配置数据 */
public:
    // 用于与关卡/序列绑定的 Tag
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attribute")
    FName MyBindingTag;

    // 从数据表读取并缓存的行数据（运行时视图）
    UPROPERTY(BlueprintReadOnly, Category = "Enemy|Attribute")
    FEnemyEntityData EnemyRow;

    // 当前生命值（运行时）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attribute")
    float Health;

    // 最大生命值（设计人员可编辑，通常从数据表初始化）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attribute")
    float MaxHealth;

    // 怪物总表引用（在网络上复制，以便客户端也能获取配置）
    UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    UDataTable* EnemyDataTable;

    // 数据表行索引，复制时触发 OnRep_EnemyData
    UPROPERTY(ReplicatedUsing = OnRep_EnemyData, EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    int32 EnemyIndex = -1;

    // 模型变体索引，复制时触发 OnRep_EnemyData
    UPROPERTY(ReplicatedUsing = OnRep_EnemyData, EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    int32 EnemyMeshIndex = -1;

    // 击杀时给予的奖励类型（设计时设置，非必须复制）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Reward")
    EWeaponRewardType RewardType = EWeaponRewardType::None;

    // IBindableEntity 接口实现
    virtual FName GetBindingTag() const override { return MyBindingTag; }
    virtual USequenceSyncComponent* GetSequenceSyncComponent() const override { return SyncComp; }
#pragma endregion


#pragma region /* 战斗与伤害逻辑 */
public:
    // 引擎的伤害入口重写，处理点伤害与通用伤害路由（服务器应为权威）
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

    // 用于剧情杀死或序列播放时触发自动死亡（仅服务器调用）
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Enemy|Combat")
    void AutoDie();

protected:
    // 在服务器上执行的核心死亡逻辑，bIsAutoDeath 区分剧情杀
    virtual void Die(bool bIsAutoDeath = false);

    // 标识是否死亡，复制时触发 OnRep_IsDead 以在客户端处理表现
    UPROPERTY(ReplicatedUsing = OnRep_IsDead, BlueprintReadOnly, Category = "Enemy|Combat")
    bool bIsDead;

    // 标明是否为剧情/强制死亡，客户端可据此决定是否播放掉落/计分
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy|Combat")
    bool bIsAutoDead;

    // 服务器记录的最后一次子弹方向，用于在客户端施加物理冲量
    FVector ServerLastShotDirection;

    // 服务器记录的最后一次命中骨骼名，用于对准骨骼施加冲量
    FName ServerLastHitBone;

    // bIsDead 的 RepNotify，用于在客户端触发死亡过渡与特效
    UFUNCTION()
    virtual void OnRep_IsDead();
#pragma endregion


#pragma region /* 死亡表现分发 */
public:
    // 多播：让所有客户端同时播放相同的死亡动画/物理表现（可靠）
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PerformDeath(EEnemyDeathType DeathType, bool bIsAuto, FVector ShotDirection, FName HitBoneName);

    // 播放基于动画的死亡表现（可在蓝图调用）
    UFUNCTION(BlueprintCallable, Category = "Enemy|Death")
    void AnimDie();

    // 播放基于物理的死亡（布娃娃），并对指定骨骼施加冲量
    UFUNCTION(BlueprintCallable, Category = "Enemy|Death")
    void PhysicsDie(FVector ShotDirection, FName HitBoneName);

    // 播放死亡特效或直接销毁逻辑（蓝图可实现）
    UFUNCTION(BlueprintCallable, Category = "Enemy|Death")
    void NiaDie();

protected:
    // 在所有客户端为尸体添加冲量（不可靠，视觉偏差可接受）
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_AddCorpseImpulse(FVector Impulse, FName BoneName);
#pragma endregion


#pragma region /* 特效与视觉 */
protected:
    // 用于受击闪光的曲线资源（通常 0->1->0）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
    class UCurveFloat* HitFlashCurve;

    // 时间轴实例，用于采样 HitFlashCurve
    FTimeline HitFlashTimeline;

    // 时间轴采样回调，用来更新材质或网格的亮度参数
    UFUNCTION()
    void HandleHitFlashProgress(float Value);

    // 在本地启动受击闪光并多播给其他客户端
    UFUNCTION(BlueprintCallable, Category = "Enemy|Effects")
    void PlayHitFlash();

    // 多播：在所有客户端播放受击闪光（不可靠，仅视觉）
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayHitFlash();
#pragma endregion


#pragma region /* 蓝图表现接口 */
protected:
    // 蓝图事件：死亡时触发，设计人员可在蓝图中添加加分、掉落、额外特效等逻辑
    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Event")
    void ReceiveOnDeath(bool bIsAuto);

    // 蓝图事件：受伤时触发，设计人员可用来更新血条或播放受击反馈
    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Event")
    void ReceiveOnTakeDamage(float CurrentHealth, float DamageAmount);
#pragma endregion


#pragma region /* 辅助工具 */
public:
    // 启用或禁用角色根或网格体的重力
    UFUNCTION(BlueprintCallable, Category = "Enemy|Utility")
    void EnableGravity(bool bEnable);

    // 将尸体设置为死亡时的通用碰撞状态
    UFUNCTION(BlueprintCallable, Category = "Enemy|Utility")
    void SetDieCollisionEnabled();

    // 动画死亡时的碰撞设置（例如先播放动画再禁用碰撞）
    UFUNCTION(BlueprintCallable, Category = "Enemy|Utility")
    void SetDieCollisionEnabled_Anim();

    // 物理死亡（布娃娃）时启用碰撞
    UFUNCTION(BlueprintCallable, Category = "Enemy|Utility")
    void SetDieCollisionEnabled_Physics();

    UFUNCTION(NetMulticast,Reliable)
    void Multicast_DisableMeshCollision();
#pragma endregion


#pragma region /* QTE 系统变量 */
public:
    // 本怪物用于 QTE 的 Actor 类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|QTE")
    TSubclassOf<class AQteBase_New> QteClassToSpawn;

    // QTE 的生成配置（数量、时长、缩放、附着骨骼等）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|QTE")
    FEnemySpawnQte_VR EnemySpawnQTE;

    // 当一批 QTE 生成完毕时广播的事件
    UPROPERTY(BlueprintAssignable, Category = "Enemy|QTE Events")
    FOnQteSpawnFinishedDelegate OnQteSpawnFinishedEvent;

    // 当整个 QTE 阶段结束时广播（参数表示阶段是否成功）
    UPROPERTY(BlueprintAssignable, Category = "Enemy|QTE Events")
    FOnQtePhaseFinishedDelegate OnQtePhaseFinishedEvent;

    // 运行时 QTE 状态追踪
    int32 CurrentSpawnNumber = 0;
    int32 KilledQteNumber = 0;
    TArray<AQteBase_New*> OwnedQte;
    FTimerHandle SpawnQteTimerHandle;
#pragma endregion


#pragma region /* QTE 系统函数 */
public:
    // 启动 QTE 阶段（服务器调用）
    UFUNCTION(BlueprintCallable, Category = "Enemy|QTE")
    void StartQTEPhase(FEnemySpawnQte_VR InSpawn);

    // 定时器回调：生成下一个 QTE
    UFUNCTION()
    void SpawnNextQTE();

    // QTE 被销毁时的回调函数（报告是否被玩家击毁）
    void OnQteDestroyed(bool bKilledByPlayer, AQteBase_New* DestroyedQTE);

    // 解析并结束 QTE 阶段，广播阶段结果
    void ResolveQTEPhase();
#pragma endregion


#pragma region /* 召唤小怪系统 */
public:
    /**
     * 在战斗中召唤小怪。
     * @param InMinionClass 要生成的小怪类（使用 AEnemyBase_New 以便复用配置）
     * @param InSpawnData 生成参数（数量、间隔、血量等）
     * @param InEnemyIndices 每只小怪对应的 EnemyIndex 数组
     * @param InEnemyMeshIndices 每只小怪对应的 MeshIndex 数组
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Enemy|Spawn")
    void SpawnMinions(TSubclassOf<class AEnemyBase_New> InMinionClass, FSpawnEnemyStruct InSpawnData, TArray<int32> InEnemyIndices, TArray<int32> InEnemyMeshIndices);

protected:
    // 小怪生成的定时器回调
    UFUNCTION()
    void SpawnMinionCallBack();

    // 小怪生成运行时状态
    FTimerHandle TimerHandle_MinionSpawn;
    int32 CurrentMinionSpawnNumber = 0;
    FSpawnEnemyStruct ActiveMinionSpawnData;
    TArray<int32> ActiveMinionIndices;
    TArray<int32> ActiveMinionMeshIndices;
    TSubclassOf<class AEnemyBase_New> MinionClassToSpawn;
#pragma endregion
};