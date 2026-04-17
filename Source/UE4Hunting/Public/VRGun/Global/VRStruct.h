#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "VRGun/Global/Enum.h"
#include "VRStruct.generated.h"

#pragma region /* 敌人与生成器数据结构 */

/**
 * 敌人配置数据结构体
 * 继承自 FTableRowBase 以支持 Data Table (数据表格) 驱动。
 * 包含了敌人从外观、碰撞、动画到死亡表现的所有核心参数。
 */
USTRUCT(BlueprintType)
struct FEnemyEntityData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<USkeletalMesh*> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TArray<UNiagaraSystem*> MeshNia;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic")
	int32 Score;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TArray<TSubclassOf<UAnimInstance>> AbpClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logic")
	FName SpecialBoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FVector2D CollisionCylinderSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FTransform MeshTranform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FTransform ShadowTranform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	USoundBase* DieMusic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	UNiagaraSystem* DieNiagara;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	EEnemyDeathType DieType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	UAnimMontage* DieAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	float ImpulseMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	float Zmultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	FName NiaSpawnBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool IsOpenAnimDieCheck;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	UStaticMesh* StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	UParticleSystem* DieParticles;

	// 构造函数：为所有非数组类型的属性提供默认安全值
	FEnemyEntityData()
		: Score(0)
		, SpecialBoneName(NAME_None)
		, CollisionCylinderSize(FVector2D(34.0f, 88.0f))
		, MeshTranform(FTransform::Identity)
		, ShadowTranform(FTransform::Identity)
		, DieMusic(nullptr)
		, DieNiagara(nullptr)
		, DieType(EEnemyDeathType::None)
		, DieAnim(nullptr)
		, ImpulseMultiplier(1.0f)
		, Zmultiplier(1.0f)
		, NiaSpawnBone(NAME_None)
		, IsOpenAnimDieCheck(false)
		, StaticMesh(nullptr)
		, DieParticles(nullptr)
	{
		// 架构说明：TArray 容器在 C++ 中会自动初始化为空，切勿在初始化列表中赋 nullptr。
	}
};

/**
 * 敌人生成指令结构体
 * 供关卡序列或蓝图事件触发定点、定量的怪物生成逻辑。
 */
USTRUCT(BlueprintType)
struct FSpawnEnemyStruct
{
	GENERATED_BODY()
	//生成数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
	int32 SpawnNumber = 0;
	//生成延迟时间
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
	float SpawnDelayTime = 1.0f;
	//绑定的Tag
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
	TArray<FName> SequenceTags;
	//血量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
	TArray<float> MaxHp;
	//奖励类型
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
	TArray<EWeaponRewardType> RewardTypes;
};

#pragma endregion


#pragma region /* 武器与表现数据结构 */

/**
 * 武器运行时动态属性数据表
 * 控制武器的核心射击算法参数、伤害以及动画表现。
 */
USTRUCT(BlueprintType)
struct FWeaponStatData : public FTableRowBase
{
	GENERATED_BODY()

	/*武器类型*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	EVRWeaponType WeaponType = EVRWeaponType::Normal;

	/*基础伤害*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Damage = 10.0f;

	/*开火距离*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float FireRange = 5000.0f;

	/** 射线数量 (单发=1，霰弹枪=多根) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 TraceCount = 1;

	/** 圆锥形散射角度限制 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float SpreadAngle = 0.0f;

	/** 水平切割枪专用的射线间距 (角度) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|Horizontal")
	float HorizontalSpreadGap = 5.0f;

	/** 连锁闪电枪：主射线初始索敌判定半径 (厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|ChainGun")
	float ChainPrimarySweepRadius = 40.0f;

	/** 连锁闪电枪：最大跳跃/弹射次数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|ChainGun")
	int32 ChainMaxBounces = 3;

	/** 连锁闪电枪：闪电跳跃的最大寻敌半径 (厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|ChainGun")
	float ChainBounceRadius = 800.0f;

	/** 连锁闪电枪：每次跳跃后的伤害保留比例 (1.0 为无衰减) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|ChainGun")
	float ChainDamageFalloff = 0.8f;

	/** 射速 (每秒开火次数)。0 或负数代表必须单发点按 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float FireRate = 10.0f;

	/*第一人称开火动画*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|Animation")
	class UAnimMontage* CharacterFireMontage = nullptr;

	/*第三人称开火动画*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats|Animation")
	class UAnimMontage* ThirdPersonFireMontage = nullptr;

	FWeaponStatData() {}
};

/**
 * 命中材质粒子特效集合
 * 封装各材质对应的 Niagara 系统，供 VisualProjectile 查阅播放。
 */
USTRUCT(BlueprintType)
struct FProjectileVFXs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Enemy = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Floor = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Water = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Rock = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Wood = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* QTE = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Dirt = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* Metal = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX") class UNiagaraSystem* DefaultHit = nullptr;

	FProjectileVFXs() {}
};

/**
 * 武器装备与替换信息同步结构体
 * 记录要生成的武器实体类、数据表行名以及是否为限时武器。
 */
USTRUCT(BlueprintType)
struct FWeaponEquipInfo
{
	GENERATED_BODY()

	/*武器类*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equip")
	TSubclassOf<class AVRWeaponActor> WeaponClass;

	/*武器表格行命名*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equip")
	FName WeaponRowName;

	/** 武器持续时间 (0 代表永久武器) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equip")
	float Duration = 0.0f;
};

#pragma endregion

#pragma region /* 生成QTE结构 */

USTRUCT(BlueprintType)
struct FEnemySpawnQte_VR
{
	GENERATED_BODY()

public:
	// 生命周期 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	TArray<double> DurationTime;

	// 每个生成的延迟时间 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	double DelaySpawnTime = 0.0;

	// 生成的数量 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	int32 SpawnNumber = 0;

	// 最大血量 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	TArray<double> MaxHp;

	// 碎裂特效大小 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	TArray<double> QteScale;

	// qte等级1-3 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	TArray<int32> QTELevel;

	// 要附加的插槽名称 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	TArray<FName> AttachBoneName;

	// 是否应用伤害 [cite: 2]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	bool bIsApplyDamage = true;

	// qte没完成时拥有者受不受伤害 [cite: 2]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	bool bQteFailedIsApplyDamage = false;

	// 造成的伤害 [cite: 2]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	double OwnerDamage = 0.0;

	// 没打爆是否伤害玩家 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	bool bIsHitPawn = false;

	// 玩家受击类型枚举 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	EPlayerHitType HitType = EPlayerHitType::Default;

	// 对玩家的伤害 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	double PawnDamage = 0.0;

	// 播放速率 [cite: 2]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemySpawnQTE")
	double PlayRate = 1.0;

	// 默认构造函数，确保所有基本数据类型都有初始值
	FEnemySpawnQte_VR()
	{
	}
};


#pragma endregion