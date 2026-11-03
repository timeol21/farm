#include "business_layer/command/mqtt_command.h"

#include "business_layer/command/command_service.h"
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_response.h"

MQTTCommandController::MQTTCommandController(ICommandService& commandService, ILobbyService& lobby)
    : m_commandService(commandService), m_lobby(lobby) {}

HttpResponse MQTTCommandController::handleHttp(const std::string& topic, const std::string& payload) {
    (void)topic;
    (void)payload;
    return HttpResponse{};
}

void MQTTCommandController::handleMqtt(const std::string& topic, const std::string& payload) {
    // 命令服务内部已经吞掉所有异常(JSON 解析/字段缺失/业务异常), MQTT 链路不会因一条坏报文退出
    m_commandService.receive(m_lobby, topic, payload);
}

std::vector<std::string> MQTTCommandController::subscriptionTopics() const {
    return m_commandService.subscriptionTopics();
}
