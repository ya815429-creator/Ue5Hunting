#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IBindableEntity.generated.h"

UINTERFACE(MinimalAPI)
class UIBindableEntity : public UInterface
{
	GENERATED_BODY()
};

/**
 * 可绑定实体约束接口
 * 规定了所有能够与关卡定序器 (Level Sequence) 进行挂钩联动的 Actor 必须实现的基础抽象方法。
 */
class UE4HUNTING_API IIBindableEntity
{
	GENERATED_BODY()

#pragma region /* 定序器绑定约束接口 */
public:
	/**
	 * 获取该实体对象用于匹配 Sequence Actor Track 的专属 Tag 标识
	 * (例如分配给主机的 "Player_0", 或通过生成器动态分配给怪物的 "Monster_01")
	 */
	virtual FName GetBindingTag() const = 0;

	/**
	 * 获取该实体对象内部挂载的序列同步组件 (SequenceSyncComponent)
	 * 以供 GameState 在切换过场动画时，统筹分发纯本地端的绑定执行指令。
	 */
	virtual class USequenceSyncComponent* GetSequenceSyncComponent() const = 0;
#pragma endregion
};