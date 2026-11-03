#include "business_layer/command/command_service.h"

#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/topic_resolver.h"
#include "business_layer/lobby/lobby_service.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"

#include <ctime>

CommandService::CommandService(ICommandDao& commandDao) : m_commandDao(commandDao) {}

void CommandService::attachMqtt(MqttService& mqtt) {
    m_mqtt = &mqtt;
    LOG_INFO("[CommandService] 上行通道已注入(MQTT)");
}

void CommandService::setSenderForTest(std::function<void(const Command::Message&)> sender) {
    m_sender = std::move(sender);
}

// ==================== 收 ====================

void CommandService::receive(ILobbyService& lobby, const std::string& topic, const std::string& payload) {
    const auto& config = ConfigManager::instance();

    std::string topicKey;
    if (!TopicResolver::resolve(config.mqtt().topics, config.box().boxNo, topic, topicKey)) {
        // 旧协议主题(配置更新/设备状态/电磁阀)已不再订阅, 到这里只可能是拼错的主题
        LOG_WARNING("[CommandService] 未识别的主题, topic=" + topic);
        return;
    }

    Command::Endpoint endpoint;
    if (!Command::fromKey(topicKey, endpoint)) {
        LOG_WARNING("[CommandService] 主题 key 未映射到端点, topic=" + topic);
        return;
    }
    if (endpoint != Command::Endpoint::COMMAND && endpoint != Command::Endpoint::READ &&
        endpoint != Command::Endpoint::REBOOT) {
        LOG_WARNING("[CommandService] 上行端点不应接收消息, topic=" + topic);
        return;
    }

    if (payload.empty()) {
        LOG_WARNING("[CommandService] 收到空 payload, topic=" + topic);
        return;
    }

    // 一次云端命令的追踪 id, 下游大厅/洗车服务共用
    trace::cmdId = std::to_string(trace::nowMs());

    try {
        json body = json::parse(payload);
        Command::fillDefaults(body);

        auto command = Command::create(endpoint, body);
        if (!command) {
            LOG_WARNING("[CommandService] 不认识的命令, topic=" + topic + ", payload=" + payload);
            sendFailure(endpoint, "unsupported command", 400);
            return;
        }

        if (!command->parse(body)) {
            LOG_WARNING("[CommandService] 命令报文字段不全, topic=" + topic + ", payload=" + payload);
            sendFailure(endpoint, "invalid command payload", 400);
            return;
        }

        const Command::Result result = command->execute(lobby);
        send(command->reply(result));
    } catch (const json::parse_error& e) {
        LOG_ERROR(std::string("[CommandService] JSON 解析失败, topic=") + topic
                  + ", payload=" + payload + ", err=" + e.what());
        // 与改造前一致: 只有命令类主题回失败回执
        if (endpoint == Command::Endpoint::COMMAND || endpoint == Command::Endpoint::REBOOT) {
            sendFailure(endpoint, "invalid json payload", 400);
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("[CommandService] 命令执行异常, topic=") + topic
                  + ", payload=" + payload + ", err=" + e.what());
        if (endpoint == Command::Endpoint::COMMAND || endpoint == Command::Endpoint::REBOOT) {
            sendFailure(endpoint, "command failed", 500);
        }
    } catch (...) {
        LOG_ERROR("[CommandService] 未知异常, topic=" + topic + ", payload=" + payload);
    }
}

// ==================== 发 ====================

bool CommandService::send(const Command::Message& message) {
    return publish(message);
}

bool CommandService::publish(const Command::Message& message) {
    LOG_INFO("[CommandService] 上行 topicKey=" + message.topicKey
             + ", bytes=" + std::to_string(message.payload.size())
             + ", type=" + std::to_string(static_cast<int>(message.type)));

    if (m_sender) {
        m_sender(message);
        return true;
    }
    if (m_mqtt != nullptr) {
        // 只传逻辑 key, MqttService 内部按 config.json 拼真实主题(缓冲/补发共用同一次解析)
        m_mqtt->publishWithBuffer(message.topicKey, message.payload, message.type);
        return true;
    }
    LOG_ERROR("[CommandService] 上行通道未注入, 报文丢弃, topicKey=" + message.topicKey);
    return false;
}

bool CommandService::sendReport(Command::Endpoint endpoint, const std::string& payload,
                                MessageBuffer::MessageType type) {
    return send(Command::Message{Command::keyOf(endpoint), payload, type});
}

void CommandService::sendFailure(Command::Endpoint requestEndpoint, const std::string& message, int code) {
    json body;
    body["message"] = message;
    body["code"] = code;
    body["time"] = static_cast<std::int64_t>(std::time(nullptr));
    send(Command::Message{Command::keyOf(replyEndpointOf(requestEndpoint)), body.dump(),
                          MessageBuffer::MessageType::TYPE_WASHCAR_RESULT});
}

Command::Endpoint CommandService::replyEndpointOf(Command::Endpoint requestEndpoint) {
    return requestEndpoint == Command::Endpoint::READ ? Command::Endpoint::READDATA
                                                     : Command::Endpoint::COMMANDREPLY;
}

// ==================== 命令记录 ====================

void CommandService::recordCommand(Command::Type type, const std::string& deviceId) {
    CommandEntity entity(type, deviceId);
    if (!m_commandDao.insertCommand(entity)) {
        LOG_ERROR("[CommandService] 命令落库失败, cmdId=" + entity.cmdId
                  + ", type=" + std::to_string(static_cast<int>(entity.type))
                  + ", deviceId=" + entity.deviceId);
    }
}

// ==================== 状态 ====================

bool CommandService::isMqttConnected() const {
    return m_mqtt != nullptr && m_mqtt->isConnected();
}

std::vector<std::string> CommandService::subscriptionTopics() const {
    const auto& config = ConfigManager::instance();
    std::vector<std::string> topics;
    for (Command::Endpoint endpoint : {Command::Endpoint::COMMAND, Command::Endpoint::READ,
                                       Command::Endpoint::REBOOT}) {
        topics.push_back(TopicResolver::build(config.mqtt().topics, config.box().boxNo,
                                              Command::keyOf(endpoint)));
    }
    return topics;
}
