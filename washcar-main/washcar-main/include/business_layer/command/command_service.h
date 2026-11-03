#pragma once

class ILobbyService;
class MqttService;

#include "business_layer/command/command_object.h"
#include "data_layer/command/command_dao.h"

#include <functional>
#include <string>
#include <vector>

// 命令服务: 云端消息的收发总口。
//   收: 主题反查端点 -> 造命令对象 -> 大厅执行业务 -> 命令对象出回执
//   发: 业务只给"端点 + 报文", 主题由这里按 config.json 生成
class ICommandService {
public:
    virtual ~ICommandService() = default;

    // ===== 收 =====
    // 云端消息入口(MQTT 回调只调这一个)
    virtual void receive(ILobbyService& lobby, const std::string& topic, const std::string& payload) = 0;

    // ===== 发 =====
    virtual bool send(const Command::Message& message) = 0;
    virtual bool sendReport(Command::Endpoint endpoint, const std::string& payload,
                            MessageBuffer::MessageType type) = 0;

    // ===== 台账 / 状态 =====
    virtual void recordCommand(Command::Type type, const std::string& deviceId) = 0;
    virtual bool isMqttConnected() const = 0;
    virtual std::vector<std::string> subscriptionTopics() const = 0;
};

class CommandService : public ICommandService {
public:
    explicit CommandService(ICommandDao& commandDao);
    ~CommandService() override = default;

    // 生产: 注入 MQTT 传输; 测试: 用 setSenderForTest 记录报文。
    // 不再单独定义通道接口 —— 传输只是这个服务的一个可替换出口。
    void attachMqtt(MqttService& mqtt);
    void setSenderForTest(std::function<void(const Command::Message&)> sender);

    void receive(ILobbyService& lobby, const std::string& topic, const std::string& payload) override;

    bool send(const Command::Message& message) override;
    bool sendReport(Command::Endpoint endpoint, const std::string& payload,
                    MessageBuffer::MessageType type) override;

    void recordCommand(Command::Type type, const std::string& deviceId) override;

    bool isMqttConnected() const override;
    std::vector<std::string> subscriptionTopics() const override;

private:
    // 请求端点 -> 回执端点(READ 回 readdata, 其余回 commandreply)
    static Command::Endpoint replyEndpointOf(Command::Endpoint requestEndpoint);
    // 失败回执(不认识的主题/命令、报文非法都在这里回)
    void sendFailure(Command::Endpoint requestEndpoint, const std::string& message, int code);
    bool publish(const Command::Message& message);

    ICommandDao& m_commandDao;
    MqttService* m_mqtt = nullptr;
    std::function<void(const Command::Message&)> m_sender;
};
