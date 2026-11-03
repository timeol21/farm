#pragma once

class ICommandService;
class ILobbyService;
class HttpResponse;

#include "business_layer/command/controller.h"

#include <string>
#include <vector>

// MQTT 入口适配器: 只把"主题 + payload"转给命令服务。
// 解析、造命令、执行业务、回执、订阅主题全部在命令服务里, 这里不再有 if-else 路由。
class MQTTCommandController : public IController {
public:
    MQTTCommandController(ICommandService& commandService, ILobbyService& lobby);

    ~MQTTCommandController() = default;

    HttpResponse handleHttp(const std::string& topic, const std::string& payload) override;

    void handleMqtt(const std::string& topic, const std::string& payload) override;

    std::vector<std::string> subscriptionTopics() const override;

private:
    ICommandService& m_commandService;
    ILobbyService& m_lobby;
};
