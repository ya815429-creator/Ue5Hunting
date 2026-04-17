#include "VRGun/UDP/FServerDiscoveryWorker.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Async/Async.h"
#include "Networking.h"
#include "SerialPort/JsonTool.h"

FServerDiscoveryWorker::FServerDiscoveryWorker(bool bInIsServer, FOnServerFoundCallback InCallback)
    : bIsRunning(true), bIsServer(bInIsServer), Callback(InCallback)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem) return; // 防御性拦截

    FString JsonResult;
    JsonTool::Instance()->ReadNestedField(TEXT("NetworkSettings"), TEXT("BroadcastPort"), JsonResult);
    if (!JsonResult.IsEmpty())
    {
        Port_ = FCString::Atoi(*JsonResult);
        UE_LOG(LogTemp, Log, TEXT("[UDP] 读取到 BroadcastPort = %d"), Port_);
    }

    if (bIsServer)
    {
        // 服务端创建广播 Socket
        Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("BroadcastSocket"), false);
        if (Socket) // 🌟 修复2：严谨的判空保护
        {
            Socket->SetBroadcast(true);
        }

        // 2. 💡 核心逻辑：获取本机正在使用的主要物理网卡 IP
        bool bCanBindAll;
        TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);

        // 端口设为 0，意思是让操作系统给这个发送 Socket 随便分配一个闲置的本地端口
        LocalAddr->SetPort(0);

        // 3. 将 Socket 强行绑定到这个物理网卡上
        if (Socket->Bind(*LocalAddr))
        {
            // 打印出来看看，UE 找的物理网卡对不对（正常应该打印出 192.168.31.143）
            UE_LOG(LogTemp, Log, TEXT("成功绑定物理网卡 IP: %s"), *LocalAddr->ToString(false));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("绑定物理网卡失败！"));
        }
    }
    else
    {
        // 客户端绑定接收 Socket
        Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("ListenSocket"), false);
        if (Socket) // 🌟 修复2：严谨的判空保护
        {
            Socket->SetReuseAddr(true);
            Socket->SetNonBlocking(true);

            TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
            Addr->SetAnyAddress();
            Addr->SetPort(Port_);
            Socket->Bind(*Addr);
        }
    }
}

FServerDiscoveryWorker::~FServerDiscoveryWorker()
{
    if (Socket)
    {
        Socket->Close();

        // 🌟 修复5：引擎关闭时的子系统安全判定
        ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (SocketSubsystem)
        {
            SocketSubsystem->DestroySocket(Socket);
        }
        Socket = nullptr;
    }
}

uint32 FServerDiscoveryWorker::Run()
{
    while (bIsRunning)
    {
        if (bIsServer)
        {
            RunServer();
            FPlatformProcess::Sleep(1.0f); // 🌟 服务端：1秒广播一次即可，不占性能
        }
        else
        {
            RunClient();
            FPlatformProcess::Sleep(0.1f); // 🌟 客户端：提高轮询频率至 0.1 秒，保证秒连无延迟！
        }
    }
    return 0;
}

void FServerDiscoveryWorker::RunServer()
{
    //利用静态变量记录上一次打印的时间
    static double LastServerLogTime = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    bool bShouldLog = (CurrentTime - LastServerLogTime) >= 5.0; // 是否过了5秒

    if (!Socket)
    {
        if (bShouldLog)
        {
            UE_LOG(LogTemp, Error, TEXT("[UDP Server] ❌ 广播失败：Socket 为空！请检查端口是否被占用。"));
            LastServerLogTime = CurrentTime; // 刷新时间
        }
        return;
    }

    FIPv4Address BroadcastAddr = FIPv4Address(255, 255, 255, 255);
    FIPv4Endpoint Endpoint(BroadcastAddr, Port_);

    FString Msg = TEXT("VR_ARCADE_SERVER_READY");
    int32 Sent = 0;

    FTCHARToUTF8 Utf8Msg(*Msg);
    TSharedRef<FInternetAddr> Addr = Endpoint.ToInternetAddr();

    if (bShouldLog)
    {
        UE_LOG(LogTemp, Log, TEXT("[UDP Server] 准备广播. 目标端口: %d, 消息: %s, 预计发送字节: %d"), Port_, *Msg, Utf8Msg.Length());
    }

    bool bSendSuccess = Socket->SendTo((uint8*)Utf8Msg.Get(), Utf8Msg.Length(), Sent, *Addr);

    if (bSendSuccess && Sent == Utf8Msg.Length())
    {
        if (bShouldLog)
        {
            UE_LOG(LogTemp, Log, TEXT("[UDP Server] 📡 广播发送成功！实际送出 %d 字节."), Sent);
            LastServerLogTime = CurrentTime; // 正常发包，重置5秒冷却
        }
    }
    else
    {
        if (bShouldLog)
        {
            UE_LOG(LogTemp, Error, TEXT("[UDP Server] ❌ 广播发送异常！返回值: %d, 实际发送字节: %d"), (int32)bSendSuccess, Sent);
            LastServerLogTime = CurrentTime; // 异常发包，重置5秒冷却
        }
    }
}

void FServerDiscoveryWorker::RunClient()
{
    double CurrentTime = FPlatformTime::Seconds();
    if (!Socket) return;
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    if (!SocketSubsystem) return;
    TSharedRef<FInternetAddr> SenderAddr = SocketSubsystem->CreateInternetAddr();
    uint32 PendingDataSize = 0;

    // 瞬间把网卡缓冲区里堆积的所有 UDP 数据包读干
    while (Socket->HasPendingData(PendingDataSize))
    {
        // 🌟 新增：接收数据的冷却锁 (防止被局域网杂包疯狂刷屏)
        static double LastReceiveLogTime = 0.0;
        bool bShouldLogReceive = (CurrentTime - LastReceiveLogTime) >= 5.0;

        if (bShouldLogReceive) UE_LOG(LogTemp, Warning, TEXT("[UDP Client] 📥 发现网卡缓冲区有积压数据！大小: %d 字节"), PendingDataSize);

        uint8 Buffer[256];
        int32 BytesRead = 0;

        int32 ReadSize = FMath::Min((int32)PendingDataSize, 256);
        bool bReadSuccess = Socket->RecvFrom(Buffer, ReadSize, BytesRead, *SenderAddr);

        if (bReadSuccess)
        {
            FString ReceivedMsg;
            ReceivedMsg.AppendChars(reinterpret_cast<const char*>(Buffer), BytesRead);

            FString SenderFullInfo = SenderAddr->ToString(true);

            if (bShouldLogReceive)
            {
                UE_LOG(LogTemp, Log, TEXT("[UDP Client] 成功读取 %d 字节. 发件人: %s, 原始内容: %s"), BytesRead, *SenderFullInfo, *ReceivedMsg);
            }

            // 核对暗号
            if (ReceivedMsg == TEXT("VR_ARCADE_SERVER_READY"))
            {
                FString ServerIP = SenderAddr->ToString(false);

                // 🌟 核心事件：无视 5 秒冷却锁，必须立刻打印！
                UE_LOG(LogTemp, Warning, TEXT("[UDP Client] ✨ 暗号匹配成功！提取到纯净服务端 IP: %s"), *ServerIP);

                if (!ServerIP.IsEmpty())
                {
                    AsyncTask(ENamedThreads::GameThread, [this, ServerIP]() {
                        UE_LOG(LogTemp, Warning, TEXT("[UDP Client] 🚀 正在通知主线程连接服务端: %s"), *ServerIP);
                        if (Callback) Callback(ServerIP);
                        });
                }
            }
            else
            {
                // 收到杂包，走冷却锁，防止被其他软件的数据刷爆
                if (bShouldLogReceive)
                {
                    UE_LOG(LogTemp, Error, TEXT("[UDP Client] 🛑 收到杂包，暗号不匹配，已过滤丢弃！"));
                    LastReceiveLogTime = CurrentTime; // 记录时间
                }
            }
        }
        else
        {
            if (bShouldLogReceive)
            {
                UE_LOG(LogTemp, Error, TEXT("[UDP Client] ❌ Socket->RecvFrom 读取数据包失败！"));
                LastReceiveLogTime = CurrentTime;
            }
        }
    }
}
void FServerDiscoveryWorker::Stop() { bIsRunning = false; }