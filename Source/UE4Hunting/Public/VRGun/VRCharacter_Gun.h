#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VRGun/Interface/IBindableEntity.h"
#include "VRCharacter_Gun.generated.h"

/**
 * VR 第一人称射击角色基类
 * 针对局域网联机机台场景设计，包含头显数据同步、序列动画绑定、以及 1P/3P 视角分离逻辑。
 */
UCLASS()
class UE4HUNTING_API AVRCharacter_Gun : public ACharacter, public IIBindableEntity
{
    GENERATED_BODY()

public:
    AVRCharacter_Gun();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region /* 组件 */
public:
    /** * VR 原点组件
     * 用于隔离定序器(Sequencer)控制的根节点变换与头显(HMD)自身的局部空间变换，防止动画与玩家真实位移冲突。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    class USceneComponent* VROrigin;

    /** * 玩家头显摄像机
     * 锁定到物理 VR 设备的真实空间位移与旋转。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    class UCameraComponent* VRCamera;

    /** * 第一人称枪械模型
     * 附加在摄像机上随动，仅本地玩家自身可见（屏蔽 3P 模型的穿模干扰）。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    class USkeletalMeshComponent* GunMesh_1P;

    /** * 序列同步组件
     * 负责解析和执行关卡序列(Level Sequence)中的 Actor 绑定逻辑。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    class USequenceSyncComponent* SyncComp;
#pragma endregion


#pragma region /* 序列同步与接口 */
public:
    /** * 本角色的动态绑定标识 (例如 "Player_0", "Player_0_No")
      * 现在由服务器全权指派并同步给客户端！
      */
    UPROPERTY(ReplicatedUsing = OnRep_MyBindingTag, EditAnywhere, BlueprintReadWrite, Category = "VR|Sequence")
    FName MyBindingTag;

    /** RepNotify: 当客户端收到服务器下发的新 Tag 时触发 */
    UFUNCTION()
    void OnRep_MyBindingTag();

    /** 实现 IBindableEntity 接口：返回当前角色的绑定 Tag */
    virtual FName GetBindingTag() const override { return MyBindingTag; }

    /** 实现 IBindableEntity 接口：返回自身的序列同步组件指针 */
    virtual USequenceSyncComponent* GetSequenceSyncComponent() const override { return SyncComp; }
#pragma endregion


#pragma region /* VR Transform 同步 */
public:
    /** * 同步变量：头显的相对变换数据
     * 由本地客户端上传至服务器，再由服务器广播给其他客户端，用于驱动远端 3P 模型的表现。
     */
    UPROPERTY(ReplicatedUsing = OnRep_HeadTransform, BlueprintReadOnly, Category = "VR|Sync")
    FTransform Rep_HeadTransform;

    /** * 供动画蓝图 (AnimBP) 轮询获取同步后的头显变换
     * 常用于计算手臂 IK 或瞄准偏移。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Sync")
    FTransform GetSyncedHeadTransform() const { return Rep_HeadTransform; }

    /** * 缓存 3P 模型初始的相对旋转
     * 用于修正虚幻引擎角色模型默认的 -90 度 Yaw 偏航角偏移，防止模型叠加头部旋转后侧身移动。
     */
    FRotator InitialMeshRotation;

public:
    // ==========================================
    // 🌟 VR 玩家身高校准系统
    // ==========================================

    /** * 一键校准玩家身高！
     * 将玩家当前的真实头显高度，像升降机一样平移对齐到虚拟模型的头部骨骼位置。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Calibration")
    void CalibrateVRHeight();

protected:
    /** * 🌟 目标眼睛视线高度 (单位:厘米)。
       * 这是从模型“脚底板”算起的绝对相对高度。
       * 例如 165.0 代表无论谁来玩，他在游戏里的视线高度都死死锁定在 1米65。
       */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Calibration")
    float TargetEyeHeight = 165.0f;

protected:
    /** * 服务器 RPC：由本地主控客户端调用，向服务器发送最新的头显变换数据。
     * 使用 Unreliable（不可靠传输）以减少网络底层重传开销，适合高频状态同步。
     */
    UFUNCTION(Server, Unreliable, WithValidation)
    void Server_SetHeadTransform(FTransform NewTransform);

    /** * RepNotify 回调：当远端客户端收到服务器广播的头显数据时触发。
     * 当前实现留空，表现层数据的应用交由 Tick 逻辑和动画蓝图处理。
     */
    UFUNCTION()
    void OnRep_HeadTransform();

    /** * 记录上一次发送头显同步 RPC 的时间戳
     * 用于执行发送频率限制（RPC 节流），防止高帧率 VR 设备引发网络风暴。
     */
    float LastHeadSyncTime = 0.0f;

protected:
    /** 开关：是否允许底层 Tick 自动根据头显偏航角旋转躯干模型 (机台模式需关闭) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Sync")
    bool bAutoRotateMeshWithHead = true;
#pragma endregion


#pragma region /* 玩家信息 */
public:
    /** * 服务器分配的玩家物理索引 (例如 0 代表 1P 机位，1 代表 2P 机位)
     * 初始值为 -1，代表尚未分配。
     */
    UPROPERTY(ReplicatedUsing = OnRep_AssignedPlayerIndex, BlueprintReadOnly, Category = "VR|Player")
    int32 AssignedPlayerIndex = -1;

    /** * RepNotify 回调：接收到有效的玩家索引后触发。
     * 负责拼装专属的 BindingTag 并通知同步组件尝试执行场景绑定。
     */
    UFUNCTION()
    void OnRep_AssignedPlayerIndex();
#pragma endregion


#pragma region /* 动态准星 (本地) */
public:
    /** * 蓝图配置：指定需要生成的 3D 准星 Actor 类 (通常包含 WidgetComponent)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Crosshair")
    TSubclassOf<class AVRAimActor> VRAimActorClass;

    /** * 运行时缓存的 3D 准星实例指针
     * 该 Actor 仅在本地客户端生成并存在，不会消耗服务器或网络带宽。
     */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "VR|Crosshair")
    class AVRAimActor* LocalVRAimActor;
#pragma endregion
};