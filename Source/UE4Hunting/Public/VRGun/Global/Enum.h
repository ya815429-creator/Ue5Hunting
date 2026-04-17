#pragma once

#include "CoreMinimal.h"
#include "Enum.generated.h"

#pragma region /* 物理与碰撞通道定义 */

// ==========================================
// 预定义的自定义追踪与对象通道 (Trace/Object Channels)
// 注意：必须与虚幻引擎 Project Settings -> Collision 中的配置保持严格一致
// ==========================================
#define COLLISION_ENEMY                       ECC_GameTraceChannel1
#define COLLISION_NAV                         ECC_GameTraceChannel2
#define COLLISION_MAINTAIN_AMPLIFICATION      ECC_GameTraceChannel3
#define COLLISION_WOOD                        ECC_GameTraceChannel4
#define COLLISION_ROCK                        ECC_GameTraceChannel5
#define COLLISION_FLOOR                       ECC_GameTraceChannel6
#define COLLISION_WATER                       ECC_GameTraceChannel7
#define COLLISION_SKILL                       ECC_GameTraceChannel8
#define COLLISION_PROP                        ECC_GameTraceChannel9
#define COLLISION_DIRT                        ECC_GameTraceChannel10
#define COLLISION_METAL                       ECC_GameTraceChannel11

#pragma endregion


#pragma region /* 状态与类型枚举 */

/**
 * 敌人的网络状态机枚举
 * 用于在服务端和客户端之间同步怪物的行为模式
 */
UENUM(BlueprintType)
enum class EEnemyNetState : uint8
{
    None,
    Spawn,
    Idle,
    Chase,
    Attack,
    Hit,
    QTE,
    Dead
};

/**
 * 敌人死亡表现类型
 * 用于指示客户端在怪物死亡时应采取哪种视觉表现
 */
UENUM(BlueprintType)
enum class EEnemyDeathType : uint8
{
    None,
    Anim,       // 播放指定的死亡动画蒙太奇
    Physics,    // 开启物理模拟 (布娃娃系统)
    Nia         // 直接隐藏模型并播放 Niagara 粒子特效
};

/**
 * 武器类型枚举
 * 用于决定武器组件的射线算法及视觉表现分支
 */
UENUM(BlueprintType)
enum class EVRWeaponType : uint8
{
    Normal          UMETA(DisplayName = "普通枪"),
    Shotgun         UMETA(DisplayName = "散弹枪"),
    HorizontalLine  UMETA(DisplayName = "水平切割枪"),
    ChainGun        UMETA(DisplayName = "连锁闪电枪"),
    Ribbon          UMETA(DisplayName = "玩具彩带枪")
};

/**
 * 命中材质类型枚举
 * 将引擎底层的物理材质或 Actor Tag 抽象为视觉特效层可识别的类型
 */
UENUM(BlueprintType)
enum class EVRHitType : uint8
{
    None        UMETA(DisplayName = "None"),
    Enemy       UMETA(DisplayName = "Enemy"),
    Floor       UMETA(DisplayName = "Floor"),
    Water       UMETA(DisplayName = "Water"),
    Rock        UMETA(DisplayName = "Rock"),
    Wood        UMETA(DisplayName = "Wood"),
    QTE         UMETA(DisplayName = "QTE"),
    Dirt        UMETA(DisplayName = "Dirt"),
    Metal       UMETA(DisplayName = "Metal"),
    Other       UMETA(DisplayName = "Other")
};

/**
 * 怪物击杀掉落奖励类型
 * 用于通知玩家武器组件执行限时武器替换逻辑
 */
UENUM(BlueprintType)
enum class EWeaponRewardType : uint8
{
    None            UMETA(DisplayName = "无奖励"),
    HorizontalLine  UMETA(DisplayName = "水平切割枪"),
    ChainGun        UMETA(DisplayName = "连锁闪电枪")
};

#pragma endregion

#pragma region /* QTE枚举 */

UENUM(BlueprintType)
enum class EPlayerHitType : uint8
{
    Default UMETA(DisplayName = "Default"), // 默认，只有变红没有特殊UI
    Bite    UMETA(DisplayName = "Bite"),    // 咬伤
    Break   UMETA(DisplayName = "Break"),   // 砸伤
    Scratch UMETA(DisplayName = "Scratch")  // 抓伤
};
#pragma endregion