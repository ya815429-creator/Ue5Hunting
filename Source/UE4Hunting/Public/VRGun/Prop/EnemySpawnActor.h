// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRGun/Global/VRStruct.h"
#include "EnemySpawnActor.generated.h"

/**
 * 联机怪物生成器 (仅服务器运行)
 * 接受配置参数或关卡事件触发，负责实例化指定类别和配置的敌人，并主动触发定序器对齐绑定。
 */
UCLASS()
class UE4HUNTING_API AEnemySpawnActor : public AActor
{
    GENERATED_BODY()

#pragma region /* 初始化 */
public:
    AEnemySpawnActor();

protected:
    virtual void BeginPlay() override;
#pragma endregion


#pragma region /* 生成控制接口 */
public:
    /** * 触发生成流程的总控入口。
     * 可供关卡蓝图或定序器事件轨道调用，传入包含数量、血量、Tag等详细配置的数据结构。
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Spawn")
    void MainSpawn(FSpawnEnemyStruct InSpawnData);

protected:
    /** * 定时器回调函数。
     * 负责解析当前循环索引对应的参数，安全注入并 Spawn 怪物实体。
     */
    UFUNCTION()
    void SpawnCallBack();
#pragma endregion


#pragma region /* 核心配置参数 */
public:
    /** 决定要生成的怪物基础 C++ 类或蓝图类 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
    TSubclassOf<class AEnemyBase_New> EnemyClass;

    /** 映射到 DataTable 中的特定怪物行索引 (对应 "NewRow_X") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
    int32 TargetEnemyIndex = -1;

    /** 映射到特定怪物行中的 Mesh 变体数组索引 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Config")
    int32 TargetEnemyMeshIndex = -1;
#pragma endregion


#pragma region /* 内部运行状态 */
private:
    /** 负责控制分批延迟生成的定时器句柄 */
    FTimerHandle TimerHandle_Spawn;

    /** 记录当前批次已生成的怪物总数 */
    int32 CurrentSpawnNumber;

    /** 运行时缓存的生成配置数据副本，供定时器回调持续查阅 */
    FSpawnEnemyStruct ActiveSpawnData;
#pragma endregion
};