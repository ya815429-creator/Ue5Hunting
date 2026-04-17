#include "VRGun/VRCharacter_Gun.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VRGun/Component/SequenceSyncComponent.h"
#include "VRGun/Prop/VRAimActor.h"
#include "VRGun/Component/VRWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "VRGun/WeaponActor/VRWeaponActor.h"
#include "VRGun/Pawn/DirectorPawn.h"
#include "EngineUtils.h"
#include "ProSceneCaptureComponent2D.h"
#pragma region /* 构造与组件初始化 */

/**
 * 构造函数：负责设置类的默认属性、实例化所有子组件，并构建组件间的父子层级关系。
 * 针对 VR 和局域网联机机台做了特定的网络与输入优化。
 */
AVRCharacter_Gun::AVRCharacter_Gun()
{
    // 允许该 Actor 每帧调用 Tick()
    PrimaryActorTick.bCanEverTick = true;

    // 开启网络同步，包括位置和旋转的移动同步
    bReplicates = true;
    SetReplicateMovement(true);

    // 【VR 机台专用设置】
    // 禁用传统的控制器旋转输入（鼠标/手柄），因为 VR 游戏中头显(HMD)的真实物理旋转会直接驱动摄像机
    bUseControllerRotationYaw = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;

    // ==========================================
    // 1. 核心 VR 组件构建与层级绑定
    // ==========================================

    // 创建 VR 原点组件（用于隔离定序器动画的位移/旋转与头显自身的局部空间）
    VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
    VROrigin->SetupAttachment(RootComponent);

    // 创建 VR 摄像机，代表玩家双眼
    VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
    VRCamera->SetupAttachment(VROrigin);
    VRCamera->bUsePawnControlRotation = false; // 同样禁用控制器旋转
    VRCamera->bLockToHmd = true;               // 强制锁定到头显设备的真实变换

    // ==========================================
    // 2. 视觉表现与同步组件构建
    // ==========================================

    // 创建第一人称枪械模型（1P）
    GunMesh_1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("GunMesh_1P"));
    GunMesh_1P->SetupAttachment(VRCamera);     // 枪械挂载在摄像机下，随视角移动
    GunMesh_1P->SetOnlyOwnerSee(true);         // 仅本地玩家（自己）可见，其他玩家看的是3P模型
    GunMesh_1P->SetCastShadow(false);          // 1P 模型通常不投射阴影，避免视觉穿帮

    // 创建序列同步组件，用于处理关卡序列（Level Sequence）的绑定与同步
    SyncComp = CreateDefaultSubobject<USequenceSyncComponent>(TEXT("SequenceSync"));

    // 配置第三人称模型（3P，继承自 ACharacter 的 Mesh）
    if (GetMesh())
    {
        GetMesh()->SetOwnerNoSee(true);        // 自己看不见自己的 3P 模型，避免遮挡 1P 视线
        GetMesh()->bCastHiddenShadow = true;   // 即使隐藏，依然可以为其他玩家/环境投射阴影
       /* GetMesh()->SetIsReplicated(true); */     // 开启 3P 模型的网络同步
    }

    // ==========================================
    // 3. 网络与机台极致同步优化
    // ==========================================

    bAlwaysRelevant = true;                    // 强制对所有玩家保持网络相关性（适合局域网机台同屏互见）
    SetNetUpdateFrequency(120.f);              // 提高网络更新频率至 120Hz，降低视觉延迟

    // 针对局域网环境，极大降低网络平滑插值的延迟时间，追求更激进的实时位置同步
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->NetworkSimulatedSmoothLocationTime = 0.05f;
        GetCharacterMovement()->NetworkSimulatedSmoothRotationTime = 0.02f;
    }


}

#pragma endregion


#pragma region /* 生命周期与每帧逻辑 (BeginPlay & Tick) */

/**
 * 游戏开始或角色生成时调用。
 * 负责记录初始状态，并仅在本地客户端生成 3D 准星等仅本地需要的元素。
 */
void AVRCharacter_Gun::BeginPlay()
{
    Super::BeginPlay();

    // 记录 3P 模型的初始相对旋转。
    // 极其关键：因为虚幻默认的 Character 骨骼网格体通常有 -90 度的 Yaw 偏移。
    // 缓存这个初始值可以防止在 Tick 中叠加头部旋转时导致模型"侧着身子"走。
    if (GetMesh())
    {
        if (AVRCharacter_Gun* DefaultChar = GetClass()->GetDefaultObject<AVRCharacter_Gun>())
        {
            InitialMeshRotation = DefaultChar->GetMesh()->GetRelativeRotation();
        }
        else
        {
            // 兜底方案
            InitialMeshRotation = GetMesh()->GetRelativeRotation();
        }
    }

    // 动态准星生成
    // 判断是否为本地玩家（非服务器代理或模拟代理），并且蓝图中已配置了准星类
    if (IsLocallyControlled() && VRAimActorClass)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        // 生成 3D 准星 Actor
        LocalVRAimActor = GetWorld()->SpawnActor<AVRAimActor>(VRAimActorClass, GetActorLocation(), GetActorRotation(), SpawnParams);

        if (LocalVRAimActor && VRCamera)
        {
            // 将准星附加到 VR 摄像机，保持相对坐标系
            LocalVRAimActor->AttachToComponent(VRCamera, FAttachmentTransformRules::KeepRelativeTransform);

            // 将准星放置在摄像机正前方 1000 厘米（10米）处
            LocalVRAimActor->SetActorRelativeLocation(FVector(1000.f, 0.f, 0.f));
            // 翻转准星朝向，使其正面朝向玩家摄像机
            LocalVRAimActor->SetActorRelativeRotation(FRotator(0.f, 180.f, 0.f));

            // 如果角色身上有武器组件，根据当前武器类型初始化准星的 UI 样式
            if (UVRWeaponComponent* WeaponComp = FindComponentByClass<UVRWeaponComponent>())
            {
                LocalVRAimActor->UpdateCrosshairType(WeaponComp->CurrentStats.WeaponType);
            }
        }
    }
	// 防御性设计：确保准星 Actor 只存在于本地玩家，并且对导播相机不可见，避免穿模和性能问题。
    if (LocalVRAimActor)
    {
        // 第一道锁：主人专属
        LocalVRAimActor->SetOwner(this); // 确保准星认你做主人
        TArray<UPrimitiveComponent*> Comps;
        LocalVRAimActor->GetComponents<UPrimitiveComponent>(Comps);
        for (UPrimitiveComponent* Comp : Comps)
        {
            Comp->SetOnlyOwnerSee(true);
        }

        //第二道锁：打入导播黑名单，防万一！
        if (GetWorld())
        {
            for (TActorIterator<ADirectorPawn> It(GetWorld()); It; ++It)
            {
                if (ADirectorPawn* Director = *It)
                {
                    // 导播相机渲染时将直接跳过准星 Actor！
                    Director->HideActorFromDirector(LocalVRAimActor);
                    break;
                }
            }
        } 
    }
}

/**
 * 每帧执行的逻辑。
 * 主要负责：1. 本地玩家限频上传头显数据； 2. 应用头部的偏航角（Yaw）到第三人称模型上。
 */
void AVRCharacter_Gun::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. 本地玩家：头显数据上传（RPC节流）
    if (IsLocallyControlled())
    {
        float CurrentTime = GetWorld()->GetTimeSeconds();

        // 【核心修复与优化】：强制 RPC 节流！
        // VR 头显的刷新率可能高达 90Hz 或 120Hz，如果每帧发送 RPC 会导致网络风暴和严重丢包。
        // 这里限制为每 0.05 秒（最高 20 次/秒）发送一次头显变换数据。
        if (CurrentTime - LastHeadSyncTime >= 0.05f)
        {
            // 获取当前摄像机相对于原点的 Transform
            FTransform LocalTransform = VRCamera->GetRelativeTransform();

            // 发送数据到服务器
            Server_SetHeadTransform(LocalTransform);

            // 本地立刻更新 Rep_HeadTransform，保证本地表现（如动画蓝图获取数据）零延迟
            Rep_HeadTransform = LocalTransform;

            // 更新最后一次同步的时间戳
            LastHeadSyncTime = CurrentTime;
        }
    }
    // 2. 表现层：3P 模型躯干旋转同步
    // 不论是本地玩家还是通过网络接收到 Rep_HeadTransform 的远端玩家，
    // 都在这里将头部的偏航角（左右转头）应用给 3P 模型，使得身体方向随头部转动。
    if (GetMesh() && bAutoRotateMeshWithHead)
    {
        FRotator HeadRot = Rep_HeadTransform.Rotator();
        FRotator FinalRot = InitialMeshRotation; // 以带有默认偏移的初始旋转为基准
        FinalRot.Yaw += HeadRot.Yaw;             // 叠加头部的 Yaw
        GetMesh()->SetRelativeRotation(FinalRot);
    }
}

#pragma endregion


#pragma region /* 属性复制与玩家状态 */

/**
 * 注册需要通过网络同步（Replication）的变量。
 */
void AVRCharacter_Gun::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // 同步头显变换数据。
    // 使用 COND_SkipOwner：极其重要！这阻止了服务器把头显数据再同步回给本地发送者。
    // 如果不加这个，本地玩家的画面会被服务器传回来的旧数据覆盖，导致强烈的画面闪回和抽搐。
    DOREPLIFETIME_CONDITION(AVRCharacter_Gun, Rep_HeadTransform, COND_SkipOwner);

    // 同步服务器分配的玩家索引
    DOREPLIFETIME(AVRCharacter_Gun, AssignedPlayerIndex);
    DOREPLIFETIME(AVRCharacter_Gun, MyBindingTag);
}

/**
 * RepNotify 回调：当客户端收到服务器同步过来的 AssignedPlayerIndex 时触发。
 * 用于生成玩家专属的 Tag，并通知组件绑定。
 */
void AVRCharacter_Gun::OnRep_AssignedPlayerIndex()
{
    // 确保索引有效
    if (AssignedPlayerIndex != -1)
    {
        UE_LOG(LogTemp, Log, TEXT("客户端玩家索引同步: %d, Tag 已生成: %s"), AssignedPlayerIndex, *MyBindingTag.ToString());
    }
}

#pragma endregion


#pragma region /* VR Transform 同步 RPC */

/**
 * 服务器 RPC 验证函数：验证客户端传来的变换数据是否合法。
 * 这里简单返回 true 允许所有更新，实际项目中可加入防作弊校验（如位置变化过大则拒绝）。
 */
bool AVRCharacter_Gun::Server_SetHeadTransform_Validate(FTransform NewTransform)
{
    return true;
}

/**
 * 服务器 RPC 实现函数：客户端调用，仅在服务器上执行。
 * 服务器接收到客户端本地的头显数据后，更新自身的 Rep_HeadTransform 变量。
 * 因为该变量被标记为 ReplicatedUsing，随后服务器会自动将这个新数据广播给其他所有客户端。
 */
void AVRCharacter_Gun::Server_SetHeadTransform_Implementation(FTransform NewTransform)
{
    Rep_HeadTransform = NewTransform;
    if (VRCamera)
    {
        VRCamera->SetRelativeTransform(NewTransform);
    }
}

/**
 * RepNotify 回调：远端客户端收到 `Rep_HeadTransform` 更新时触发。
 * 此处留空，因为位置/旋转的具体应用策略（如 IK 骨骼控制）目前交由"动画蓝图"（AnimBP）通过 GetSyncedHeadTransform() 直接轮询获取，不需要基于事件驱动。
 */
void AVRCharacter_Gun::OnRep_HeadTransform()
{
    // 数据已到达，实际应用在动画蓝图中获取
}

#pragma endregion

#pragma region /* VR 身高校准 (纯数值版) */

void AVRCharacter_Gun::CalibrateVRHeight()
{
    // 确保组件存在 (不需要 GetMesh 了，只需胶囊体作为基准)
    if (GetCapsuleComponent() && VROrigin && VRCamera)
    {
        // 1. 获取角色“脚底板”的绝对世界坐标 Z 值
        // 算法：胶囊体中心的 Z 坐标 - 胶囊体的半高 = 完美贴合地面的脚底位置
        float FloorZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

        // 2. 算出我们期望的完美眼睛世界坐标 Z 值
        float TargetZ = FloorZ + TargetEyeHeight;

        // 3. 获取玩家当前戴着头显的真实绝对世界高度
        float CurrentHMDHeightZ = VRCamera->GetComponentLocation().Z;

        // 4. 计算“身高差” (目标高度 - 现实高度)
        float HeightDifference = TargetZ - CurrentHMDHeightZ;

        // 5. 补偿给 VROrigin
        VROrigin->AddWorldOffset(FVector(0.0f, 0.0f, HeightDifference));

        UE_LOG(LogTemp, Warning, TEXT("[VR_Gun] 数值身高校准完毕！目标视线: %.1f cm | 补偿偏移量: %.2f cm"), TargetEyeHeight, HeightDifference);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[VR_Gun] 身高校准失败：缺少胶囊体或 VR 组件！"));
    }
}

#pragma endregion

void AVRCharacter_Gun::OnRep_MyBindingTag()
{
    UE_LOG(LogTemp, Warning, TEXT("[VR_Gun] 客户端同步到最新的绑定 Tag: %s"), *MyBindingTag.ToString());

    if (SyncComp)
    {
        //强制解开客户端的本地绑定锁！
        SyncComp->bHasLocallyBound = false;

        // 1. 同步组件的 Tag
        SyncComp->BindingTag = MyBindingTag;

        // 2. 尝试绑定定序器
        // 如果是 "Player_0_No"，定序器里找不到这个轨道，安全忽略，乖乖待在小黑屋。
        // 如果是 "Player_0"，瞬间绑定并吸附上车！
        SyncComp->ApplyBindingAndSeek();
    }
}