
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRGun/Global/Enum.h"
#include "VRGun/Global/VRStruct.h"
#include "Engine/DataTable.h"
#include "VRWeaponComponent.generated.h"

/**
 * UVRWeaponComponent
 *
 * VR 武器核心逻辑组件，挂载于 Character 上。
 * 负责：
 * - 管理武器状态（装备/卸下、限时武器切换、默认武器恢复）
 * - 读取并同步武器数据表属性给客户端（CurrentStats）
 * - 处理开火流程与弹道判定（多种射线算法）
 * - 将本地计算的命中结果提交给服务器进行最终伤害裁决
 * - 管理远端持续开火的可视状态同步（用于高射速武器的 3P 表现）
 *
 * 说明：
 * - 该组件设计遵循服务器权威（Authority）模式，部分接口仅在服务端调用。
 * - 网络相关属性使用 Replicated / RepNotify 实现实时状态同步与客户端特效触发。
 */
UCLASS(ClassGroup = (Weapon), meta = (BlueprintSpawnableComponent))
class UE4HUNTING_API UVRWeaponComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVRWeaponComponent();

    /** 组件初始化入口（覆写：BeginPlay） */
    virtual void BeginPlay() override;

    /** 注册需要被复制的属性（覆写） */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region /* 缓存指针 */
protected:
    /** 
     * 零损耗调用缓存：保存常用指针，避免高频调用时频繁 Cast。
     * - CachedCharacter: 指向拥有该组件的 AVRCharacter_Gun（如有）
     * - CachedMesh3P: 角色的 3P 骨骼网格（第三人称武器视觉）
     * - CachedGunMesh1P: 第一人称武器网格（1P 视角武器）
     *
     * 标记为 Transient：不序列化到磁盘。
     */
    UPROPERTY(Transient)
    class AVRCharacter_Gun* CachedCharacter;

    UPROPERTY(Transient)
    class USkeletalMeshComponent* CachedMesh3P;

    UPROPERTY(Transient)
    class USkeletalMeshComponent* CachedGunMesh1P;
#pragma endregion


#pragma region /* 武器配置与状态 */
public:
    /** 武器总数据表：包含所有武器基础配置与属性 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
    class UDataTable* WeaponDataTable;

    /** 当前武器在数据表中对应的行名（RowName） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
    FName WeaponRowName;

    /**
     * 核心同步数据：从数据表读取并复制给客户端的实时战斗属性
     * 使用 ReplicatedUsing 指定 OnRep_CurrentStats 在客户端变化时回调（用于 UI / 表现更新）
     */
    UPROPERTY(ReplicatedUsing = OnRep_CurrentStats, BlueprintReadOnly, Category = "Weapon|State")
    FWeaponStatData CurrentStats;

    /** RepNotify 回调：当 CurrentStats 在客户端被更新时触发，用于触发额外逻辑（如准星更新） */
    UFUNCTION()
    void OnRep_CurrentStats();

    /** 通知本地准星 Widget 更新形态与材质（仅在本地调用，非网络方法） */
    void UpdateLocalCrosshair();

    /** 当前绑定的视觉表现实体 Actor（Equipped Weapon Actor 实例） */
    UPROPERTY(BlueprintReadWrite, Category = "Weapon|State")
    class AVRWeaponActor* EquippedWeapon;

    /** 
     * 存储上一次开火时所有射线命中的材质类型（枚举数组），以便蓝图或调试面板查看。
     * 只读供 UI / 蓝图使用。
     */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon|HitInfo")
    TArray<EVRHitType> LastHitTypes;


public:
    /** 伤害跳字 Actor 的类（用于在世界中显示伤害数字） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|UI")
    TSubclassOf<class AVRDamageNumberActor> DamageNumberClass;

    /** 距离过近时不生成伤害数字的阈值（单位：厘米），防止近距离过于刺眼的 UI */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|UI")
    float MinDistanceToSpawnDamage = 150.0f; // 默认 1.5 米内不显示，防眩晕

public:
    /** 武器保险：是否允许开火 */
    UPROPERTY(BlueprintReadWrite, Category = "Weapon|State")
    bool bCanFire = true;

    /** 设置武器保险，锁死时自动打断当前连发 */
    UFUNCTION(BlueprintCallable, Category = "Weapon|State")
    void SetCanFire(bool bEnable);
#pragma endregion


#pragma region /* 装备与奖励系统 */
public:
    /**
     * 掉落奖励配置映射表：
     * - Key: EWeaponRewardType（击杀奖励类型）
     * - Value: FWeaponEquipInfo（具体武器类与数据行等信息）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Reward")
    TMap<EWeaponRewardType, FWeaponEquipInfo> RewardWeaponMap;

    /**
     * 接收并执行击杀奖励换枪逻辑（服务端专用）
     * - 参数：RewardType - 奖励类型枚举
     * - BlueprintAuthorityOnly：仅在权威端可调用（防止客户端擅自切换）
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Weapon|Reward")
    void GrantWeaponReward(EWeaponRewardType RewardType);

    /**
     * 请求装备武器（可指定持续时长，用于限时武器）
     * - NewWeaponClass: 要生成的武器 Actor 类
     * - NewRowName: 数据表行名用于读取属性
     * - Duration: 武器持续时间（<=0 表示永久）
     * 
     * 该函数在客户端调用时会触发 Server_RequestEquipWeapon（RPC）以由服务器执行。
     */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Equip")
    void RequestEquipWeapon(TSubclassOf<AVRWeaponActor> NewWeaponClass, FName NewRowName, float Duration = 0.0f);

    /**
     * 将实体武器网格绑定到角色指定的插槽（1P/3P）。
     * 可被蓝图覆写以适配不同角色骨骼或手持动画。
     *
     * @param SocketName1P - 第一人称武器插槽名称（默认 NAME_None 则使用默认配置）
     * @param SocketName3P - 第三人称武器插槽名称
     */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Equip")
    virtual void BindWeaponMeshesToCharacter(FName SocketName1P = NAME_None, FName SocketName3P = NAME_None);

    /** 未投币时用于展示的默认武器配置（例如彩带枪的配置） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|DefaultConfig")
    FWeaponEquipInfo UnpaidRibbonWeapon;

    /** 兜底默认武器：当关卡没有在 MapDefaultWeaponMap 配置时使用 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|DefaultConfig")
    FWeaponEquipInfo FallbackDefaultWeapon;

    /** 各关卡地图名称对应的专属默认武器映射表（Key: 关卡名） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|DefaultConfig")
    TMap<FString, FWeaponEquipInfo> MapDefaultWeaponMap;

    /**
     * 根据投币状态与当前地图名自动分配并装备初始武器（服务端执行）
     * - BlueprintAuthorityOnly：仅服务端调用
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Weapon|Equip")
    void EquipInitialWeapon();
protected:
    /**
     * 切枪信息同步载体（RepNotify）：当该结构在客户端更新时，OnRep_WeaponEquipInfo 会被调用
     * 包含生成武器所需的类、RowName、持续时间等信息。
     */
    UPROPERTY(ReplicatedUsing = OnRep_WeaponEquipInfo)
    FWeaponEquipInfo Rep_WeaponEquipInfo;

    /** Client->Server RPC：请求服务器执行装备武器（可信通道） */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestEquipWeapon(FWeaponEquipInfo NewInfo);

    /** 当 Rep_WeaponEquipInfo 在客户端被更新时触发（负责生成/绑定视觉武器等逻辑） */
    UFUNCTION()
    void OnRep_WeaponEquipInfo();

    /** 实际执行旧武器销毁与新武器生成绑定的逻辑（内部实现） */
    void PerformWeaponSpawnAndBind();

    // ==========================================
    // 限时武器管理机制（用于临时拾取类武器）
    // ==========================================
    /** 保存拾取限时武器之前的默认永久武器信息（用于到期后恢复） */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon|State")
    FWeaponEquipInfo SavedDefaultWeaponInfo;

    /** 标记是否已备份默认武器（以防重复备份） */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon|State")
    bool bHasSavedDefaultWeapon = false;

    /** 武器到期倒计时的定时器句柄（非网络复制） */
    FTimerHandle WeaponDurationTimerHandle;

    /** 定时器回调：当限时武器到期时该函数会被调用，负责恢复先前的默认武器 */
    UFUNCTION()
    void OnWeaponDurationExpired();
#pragma endregion


#pragma region /* 开火系统与弹道裁决 */
public:
    /** 开始连续开火（由玩家或 AI 触发） */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Action")
    void StartFire();

    /** 停止连续开火（停止计时器与射击） */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Action")
    void StopFire();

protected:
    /** 开火计时器句柄（控制连发/射速） */
    FTimerHandle FireTimerHandle;

    /**
     * 获取射击起点与方向。
     * 将射击起点/方向提取为虚函数，方便机台或特殊模式覆写（例如改变发射基点）。
     *
     * @param OutStartLoc - 输出：射线起点
     * @param OutForwardDir - 输出：前向方向（单位向量）
     * @param OutUpDir - 输出：上方向（用于某些散射算法）
     */
    virtual void GetShootOriginAndDirection(FVector& OutStartLoc, FVector& OutForwardDir, FVector& OutUpDir);

    /** 开火逻辑总控：由 StartFire 驱动，在计时器触发或单发时调用 */
    void ExecuteFire();

    /** 常规步枪与霰弹枪算法：圆锥散射实现（适配弹道、命中范围） */
    void PerformHitscan_NormalAndShotgun();

    /** 水平切割枪算法：定角平行散射实现（生成多条平行射线） */
    void PerformHitscan_HorizontalLine();

    /** 连锁闪电枪算法：命中后在空间中跳跃查找下一个目标（可能递归或迭代） */
    void PerformHitscan_ChainGun();

    /** 彩带枪专属开火逻辑：仅视觉，没有射线判定（用于展示） */
    void PerformHitscan_Ribbon();

    /**
     * RPC：将本地计算好的弹道碰撞结果提交给服务器，由服务器进行最终伤害裁决。
     * - 参数为数组以支持一次开火命中多个目标（如霰弹/散射/连锁）
     * - Server, Reliable, WithValidation：服务端可信调用并带验证
     *
     * @param HitActors - 命中的 Actor 列表
     * @param TargetLocations - 命中点世界坐标数组
     * @param TargetNormals - 命中法线数组
     * @param HitTypes - 命中材质类型枚举数组
     * @param ShotDirections - 每次射线的发射方向（用于弹道与击退计算）
     * @param HitBoneNames - 命中骨骼名称（若适用，用于暴击判定）
     */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_ProcessHits(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes, const TArray<FVector>& ShotDirections, const TArray<FName>& HitBoneNames);

    /**
     * RPC：服务器裁决完毕后触发，通知所有客户端播放第三人称（3P）开火特效与动画（不可靠，用于视觉效果）
     * - NetMulticast, Unreliable：广播到所有客户端但不保证到达
     *
     * @param TargetLocations - 命中点数组（用于播放粒子/弹壳飞溅等）
     * @param TargetNormals - 命中法线数组（用于对齐特效）
     * @param HitTypes - 命中材质类型，用于选择不同的特效（如金属/木头/肉）
     */
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayFireEffects(const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<EVRHitType>& HitTypes);
#pragma endregion


#pragma region /* 工具与查询接口 */
public:
    /**
     * 重新根据 WeaponRowName 从 WeaponDataTable 解析并应用数据表数值到 CurrentStats。
     * 该函数可在蓝图或编辑器中触发以即时刷新属性。
     */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Utility")
    void InitWeaponData();

    /**
     * 局内增益接口：动态提升武器基础伤害（仅服务端调用）
     * - ExtraDamage 为要增加的伤害值（可为负用于减益）
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Weapon|Utility")
    void UpgradeDamage(float ExtraDamage);

    /**
     * 解析目标 Actor 的 Tag 或其它标记并返回对应的命中物理材质枚举（EVRHitType）。
     * - 用于决定命中特效 / 伤害类型 / 护甲效果等。
     */
    UFUNCTION(BlueprintCallable, Category = "Weapon|Utility")
    EVRHitType GetHitType(AActor* InActor);
protected:
    /**
     * 获取武器专属的碰撞通道查询参数（FCollisionObjectQueryParams）。
     * - 根据当前武器配置（如弧型、穿透等）返回合适的对象查询掩码。
     */
    FCollisionObjectQueryParams GetWeaponObjectQueryParams() const;

    /**
     * 统一处理 3D 伤害跳字的生成逻辑：
     * - 自动适配连锁闪电枪的伤害衰减（若需要对同一目标显示不同数值）
     * - 过滤过近目标（MinDistanceToSpawnDamage）以避免 UI 干扰
     *
     * @param HitActors - 命中 Actor 列表
     * @param TargetLocations - 对应命中点数组
     * @param StartLoc - 发射起点（用于距离判定）
     */
    void SpawnDamageNumbers(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const FVector& StartLoc);

    /**
     * 开火收尾：统一处理视觉表现（本地）与将命中结果提交服务器（Server_ProcessHits）
     * - 计算必要的数据数组（HitActors、TargetLocations、TargetNormals、ShotDirections、HitBoneNames）
     * - 本地播放一人称特效，并在服务器端最终裁决后通过 Multicast_PlayFireEffects 通知其他客户端播放 3P 特效
     */
    void FinalizeFire(const TArray<AActor*>& HitActors, const TArray<FVector>& TargetLocations, const TArray<FVector>& TargetNormals, const TArray<FVector>& ShotDirections, const TArray<FName>& HitBoneNames);
#pragma endregion

#pragma region /* 性能优化：远端持续开火状态同步 */
protected:
    /**
     * 网络状态同步：是否正在持续开火（仅用于远端 3P 的视觉呈现）
     * - 使用 ReplicatedUsing 指定 OnRep_IsFiringNet 用于在远端触发视觉循环
     */
    UPROPERTY(ReplicatedUsing = OnRep_IsFiringNet)
    bool bIsFiring_Net = false;

    /** 当 bIsFiring_Net 在客户端发生变化时调用（用于触发/停止远端的开火视觉定时器） */
    UFUNCTION()
    void OnRep_IsFiringNet();

    /** 向服务器提交开火状态改变的 RPC（可信） */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SetFiringState(bool bFiring);

    /** 远端自动开火视觉表现的计时器句柄（本地运行） */
    FTimerHandle AutoFireVisualTimerHandle;

    /**
     * 远端自动开火视觉模拟逻辑（仅在客户端运行）：
     * - 在 OnRep_IsFiringNet 启动的定时器回调中周期性播放射击声/火光/后坐动画以模拟持续开火
     * - 该逻辑仅用于视觉补偿，不参与伤害判定
     */
    void PlayAutoFireVisuals();
#pragma endregion
};