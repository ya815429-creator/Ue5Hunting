// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BGMManager.generated.h"

class UAudioComponent;
class USoundBase;
UCLASS()
class UE4HUNTING_API ABGMManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABGMManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	//歌单配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|BGM")
	TMap<FName, USoundBase*> BGMPlaylist;
	//状态驱动接口（进服务器调用）
	/** *通过标签切歌（如：“Lobby”）
	* @param BGMTag 歌曲标签
	*/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VR|BGM")
	void SwitchBGM(FName BGMTag);

	/*停止当前BGM*/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VR|BGM")
	void StopBGM();

protected:
	//网络同步核心变量

	/**当前正在播放的 BGM 标签。当服务器修改它时，所有客户端会自动触发 OnRep 函数 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentBGMTag)
	FName CurrentBGMTag;

	/** 客户端/导播 本地监听到标签改变时，自动触发交叉淡入淡出*/
	UFUNCTION()
	void OnRep_CurrentBGMTag();

public:
	//双通道淡入淡出系统
	UPROPERTY(VisibleAnywhere, Category = "VR|BGM")
	UAudioComponent* AudioChannelA;

	UPROPERTY(VisibleAnywhere, Category = "VR|BGM")
	UAudioComponent* AudioChannelB;

	bool bIsChannelAPlaying = true;
	//默认的淡入淡出时间
	const float CrossfadeDuration = 2.0f;
};
