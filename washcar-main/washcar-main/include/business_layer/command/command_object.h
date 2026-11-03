#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "business_layer/command/mqtt/message_buffer.h"
#include "business_layer/lobby/lobby_object.h"

#include <nlohmann/json.hpp>

class ILobbyService;

// 云端命令: 一条报文 = 一个命令对象。
// 端点、命令种类、回执报文、payload 解析全部收在这一个类里; 子类只表达业务动作。
class Command {
public:
    // ===== 嵌套类型 =====

    // 云端 7 个端点: 下行 3 个有命令对象, 上行 4 个只是报文通道
    enum class Endpoint { COMMAND, READ, REBOOT, COMMANDREPLY, READDATA, MONITOR, PUSHALARM };

    // 命令种类: 只用于落库和日志。跟端点不是一个维度 —— 一条 COMMAND 既可能是启动也可能是停止。
    enum class Type {
        SOLENOID_OPEN,   // 打开电磁阀
        SOLENOID_CLOSE,  // 关闭电磁阀
        CAMERA_HISTORY,  // 摄像头历史视频
        DEVICE_READ,     // 读设备状态
        DEVICE_CTRL,     // 通用设备控制
        FRAME_EXTRACT,   // 实时提帧
        WASHCAR_START,   // 启动洗车
        WASHCAR_STOP,    // 停止洗车
        WASHCAR_RESET,   // 复位洗车
        CLEAR_ALARM,     // 清除报警
        CHANGE_MODE,     // 进/出维修模式
        POINT_ON,        // 点位打开
        POINT_OFF,       // 点位关闭
        POINT_RESET,     // 点位复位
        POINT_STATUS,    // 点位查询
        OTHER,           // 其他业务命令
        WASHCAR_READ,    // 云端 read
        REBOOT           // 云端 reboot
    };

    // 一条待发消息: 逻辑主题 key + 报文 + 缓冲类型
    struct Message {
        std::string topicKey;
        std::string payload;
        MessageBuffer::MessageType type = MessageBuffer::MessageType::TYPE_OTHER;
    };

    // 命令执行结果(命令对象不依赖 LobbyResult 的具体形状)
    struct Result {
        bool ok = false;
        int code = 0;
        std::string message;
        std::string boxNo;
        std::unordered_map<std::string, int> points;   // read 回执用
    };

    // ===== 入口: 端点 + 报文 → 命令对象; 不认识返回 nullptr =====
    static std::shared_ptr<Command> create(Endpoint endpoint, const json& body);

    Command(Type type, std::string deviceId);
    virtual ~Command() = default;

    // ===== 生命周期: 造出来后按顺序调 =====
    virtual bool parse(const json& body) = 0;            // ① 云端报文 → 业务数据
    virtual Result execute(ILobbyService& lobby) = 0;   // ② 交给大厅执行
    virtual Message reply(const Result& result) const = 0; // ③ 组回执报文

    // ===== 身份 =====
    const std::string& id() const { return m_id; }
    Type type() const { return m_type; }
    const std::string& deviceId() const { return m_deviceId; }

    // ===== 协议元信息 =====
    virtual Endpoint endpoint() const = 0;
    virtual Endpoint replyEndpoint() const { return Endpoint::COMMANDREPLY; }
    virtual MessageBuffer::MessageType replyMessageType() const {
        return MessageBuffer::MessageType::TYPE_WASHCAR_RESULT;
    }

    // ===== topic 字符串 ↔ Endpoint 映射 (CommandService 路由用) =====
    static bool fromKey(const std::string& key, Endpoint& out);
    static const char* keyOf(Endpoint endpoint);

    // ===== 内部工具 (实现细节, 外部不需要直接调) =====
    static std::string createId();
    static void fillDefaults(json& body);   // 补 boxNo/plcId/reqSource

protected:
    static std::int64_t nowSeconds();
    Message buildReply(const Result& result) const;

private:
    std::string m_id;
    Type m_type;
    std::string m_deviceId;
};

// 洗车命令: 云端 payload -> WashCarOperation -> 大厅业务方法。
// 跟状态机里的 WashCarCommand 枚举(命令意图)不是一回事, 所以叫 CloudCommand。
// 启动/停止/复位/机型扩展现在没有行为差异, 由 create() 用 (type, name, invoker) 组装。
class WashCarCloudCommand : public Command {
public:
    using Invoker = LobbyResult<WashCarOperationResult> (ILobbyService::*)(const WashCarOperation&);

    WashCarCloudCommand(Type type, const char* name, Invoker invoker);

    Endpoint endpoint() const override { return Endpoint::COMMAND; }
    bool parse(const json& body) override;
    Result execute(ILobbyService& lobby) override;
    Message reply(const Result& result) const override;

    const WashCarOperation& operation() const { return m_operation; }

protected:
    const char* m_name;
    Invoker m_invoker;
    WashCarOperation m_operation;
};

// 云端 read: 读点位, 回执走 readdata
class WashCarReadCommand : public Command {
public:
    WashCarReadCommand();
    Endpoint endpoint() const override { return Endpoint::READ; }
    Endpoint replyEndpoint() const override { return Endpoint::READDATA; }

    bool parse(const json& body) override;
    Result execute(ILobbyService& lobby) override;
    Message reply(const Result& result) const override;

private:
    WashCarOperation m_operation;
};

// 云端 reboot: 先回执, 再延迟执行重启
class RebootCommand : public Command {
public:
    RebootCommand();
    Endpoint endpoint() const override { return Endpoint::REBOOT; }

    bool parse(const json& body) override;
    Result execute(ILobbyService& lobby) override;
    Message reply(const Result& result) const override;

private:
    bool m_valid = false;
    std::string m_boxNo;
};
