#include "presentation/mqtt/mqtt_client.h"

#include "common/log/log_manager.h"
#include "common/topics.h"

#include <mosquitto.h>

#include <algorithm>
#include <cstring>

namespace {
// mosquitto 的 lib_init/lib_cleanup 是进程级的，重复 init 会出问题。
// 用一个引用计数保证只有第一个客户端 init、最后一个客户端 cleanup。
int g_libRefCount = 0;
std::mutex g_libMutex;

void libAcquire() {
    std::lock_guard<std::mutex> lock(g_libMutex);
    if (g_libRefCount++ == 0) {
        mosquitto_lib_init();
    }
}

void libRelease() {
    std::lock_guard<std::mutex> lock(g_libMutex);
    if (--g_libRefCount == 0) {
        mosquitto_lib_cleanup();
    }
}

/// 盒子订阅的下行端点。上行只说发布，不需要订阅列表。
const char* const kSubscribeEndpoints[] = {
    FLEXEM_COMMAND_TOPIC,   // "command"
    FLEXEM_READ_TOPIC,      // "read"
    FLEXEM_REBOOT_TOPIC,    // "reboot"
};

std::string joinTopics(const std::vector<std::string>& topics) {
    std::string out;
    for (size_t i = 0; i < topics.size(); ++i) {
        if (i) out += ", ";
        out += topics[i];
    }
    return out;
}
}  // namespace

MqttClient::MqttClient() {
    libAcquire();
}

MqttClient::~MqttClient() {
    stop();
    libRelease();
}

bool MqttClient::start(const washcar::cfg::MqttConfig& cfg, const std::string& boxNo, MessageHandler handler) {
    if (running_.load()) {
        return true;
    }

    boxNo_ = boxNo;
    handler_ = std::move(handler);

    // 分隔符只认 "." 和 "/"；非法值回落到生产抓包格式 "."
    separator_ = (cfg.topicSeparator == "/") ? '/' : '.';

    // 每个 endpoint 同时订阅点号与斜杠两种写法：
    //   生产抓包  flexem.123456.command
    //   Excel 配置 flexem/123456/command
    // 两者都收，云端切写法不用改盒子。
    subscribeTopics_.clear();
    topicToEndpoint_.clear();
    for (const char* endpoint : kSubscribeEndpoints) {
        for (char sep : {'.', '/'}) {
            std::string topic = "flexem" + std::string(1, sep) + boxNo_ + sep + endpoint;
            subscribeTopics_.push_back(topic);
            topicToEndpoint_[topic] = endpoint;
        }
    }

    // clientId 直接用 boxNo：云端靠它区分设备，两台同号会互踢。
    mosq_ = mosquitto_new(boxNo_.c_str(), /*clean_session=*/true, this);
    if (!mosq_) {
        LOG_ERROR("[MQTT] mosquitto_new 失败（内存不足？）");
        return false;
    }

    if (!cfg.username.empty()) {
        mosquitto_username_pw_set(mosq_, cfg.username.c_str(), cfg.password.c_str());
    }

    mosquitto_connect_callback_set(mosq_, &MqttClient::onConnectCb);
    mosquitto_message_callback_set(mosq_, &MqttClient::onMessageCb);
    mosquitto_disconnect_callback_set(mosq_, &MqttClient::onDisconnectCb);

    // 重连退避：起始间隔来自 mqtt.reconnectBaseSec（默认 2s），指数增长，
    // 上限固定 30s —— 上限不进配置，因为再大就等于"十几分钟才重试一次"，
    // 现场网络恢复后迟迟连不回来反而更糟。
    reconnectBaseSec_  = cfg.reconnectBaseSec > 0 ? cfg.reconnectBaseSec : 2;
    reconnectMaxRetry_ = cfg.reconnectMaxRetry;      // <=0 表示不告警，只重试
    reconnectAttempts_.store(0);
    mosquitto_reconnect_delay_set(mosq_, static_cast<unsigned int>(reconnectBaseSec_),
                                  /*max_delay=*/30u, /*exponential_backoff=*/true);

    const int keepalive = cfg.heartbeatSec > 0 ? cfg.heartbeatSec : 30;
    const int rc = mosquitto_connect_async(mosq_, cfg.brokerIp.c_str(), cfg.port, keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR(std::string("[MQTT] connect_async 失败: ") + mosquitto_strerror(rc));
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    running_ = true;
    // loop_start 起后台线程，断线重连由库自己处理
    const int loopRc = mosquitto_loop_start(mosq_);
    if (loopRc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR(std::string("[MQTT] loop_start 失败: ") + mosquitto_strerror(loopRc));
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        running_ = false;
        return false;
    }

    LOG_INFO(std::string("[MQTT] 正在连接 ") + cfg.brokerIp + ":" + std::to_string(cfg.port)
             + ", clientId=" + boxNo_ + ", 发布前缀 " + buildTopic("<endpoint>"));
    return true;
}

void MqttClient::stop() {
    if (!mosq_) {
        return;
    }
    running_ = false;
    connected_ = false;

    mosquitto_disconnect(mosq_);
    mosquitto_loop_stop(mosq_, false);   // false = 不强制杀线程，等它自己退出
    mosquitto_destroy(mosq_);
    mosq_ = nullptr;

    LOG_INFO("[MQTT] 已停止");
}

std::string MqttClient::buildTopic(const std::string& endpoint) const {
    return "flexem" + std::string(1, separator_) + boxNo_ + separator_ + endpoint;
}

bool MqttClient::publish(const std::string& endpoint, const std::string& payload) {
    if (!mosq_) {
        return false;
    }
    if (!connected_.load()) {
        // 未连接直接丢弃：本精简版不做落库补发，避免把磁盘写入拖进主链路
        LOG_WARNING(std::string("[MQTT] 未连接，丢弃消息: ") + endpoint);
        return false;
    }

    const std::string topic = buildTopic(endpoint);
    std::lock_guard<std::mutex> lock(publishMutex_);
    // QoS 0 + retain false —— 平台硬约束
    const int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),
                                     static_cast<int>(payload.size()), payload.data(),
                                     /*qos=*/0, /*retain=*/false);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARNING(std::string("[MQTT] publish 失败: ") + mosquitto_strerror(rc) + ", topic=" + topic);
        return false;
    }
    return true;
}

void MqttClient::onConnectCb(struct mosquitto* /*m*/, void* obj, int rc) {
    static_cast<MqttClient*>(obj)->handleConnect(rc);
}

void MqttClient::onMessageCb(struct mosquitto* /*m*/, void* obj, const struct mosquitto_message* msg) {
    auto* self = static_cast<MqttClient*>(obj);
    if (!msg || !msg->topic) {
        return;
    }
    const std::string topic(msg->topic);
    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign(static_cast<const char*>(msg->payload), static_cast<size_t>(msg->payloadlen));
    }
    self->handleMessage(topic, payload);
}

void MqttClient::onDisconnectCb(struct mosquitto* /*m*/, void* obj, int rc) {
    auto* self = static_cast<MqttClient*>(obj);
    self->connected_ = false;

    const int attempt = self->reconnectAttempts_.fetch_add(1) + 1;
    const int maxRetry = self->reconnectMaxRetry_;

    // 断线日志按次数分档，避免网络长期不通时每分钟刷几十条：
    //   前 3 次         WARNING（正常抖动，运维能看到）
    //   达到 maxRetry+1 ERROR 一次（明确提示"可能是配置写错了"）
    //   之后            DEBUG（只进文件，不刷终端）
    if (maxRetry > 0 && attempt == maxRetry + 1) {
        LOG_ERROR("[MQTT] 已连续重连 " + std::to_string(maxRetry)
                  + " 次仍未成功，请检查 brokerIp / 端口 / 账号密码 / 网络。"
                    "（库会继续重试，不会退出进程）");
    } else if (attempt <= 3) {
        LOG_WARNING(std::string("[MQTT] 连接断开, rc=") + std::to_string(rc)
                    + "（第 " + std::to_string(attempt) + " 次重试即将开始）");
    } else {
        LOG_DEBUG("[MQTT] 重连中, attempt=" + std::to_string(attempt)
                  + ", rc=" + std::to_string(rc));
    }
}

void MqttClient::handleConnect(int rc) {
    if (rc != 0) {
        connected_ = false;
        LOG_ERROR(std::string("[MQTT] 连接被拒, rc=") + std::to_string(rc)
                  + "（1=协议版本 2=clientId 被拒 4=账号密码错 5=未授权）");
        return;
    }
    connected_ = true;
    if (reconnectAttempts_.exchange(0) > 0) {
        LOG_INFO("[MQTT] 重连成功，失败计数已清零");
    }
    LOG_INFO(std::string("[MQTT] 已连接, 发布前缀 ") + buildTopic("<endpoint>"));

    // 每次(重)连都要重订阅：cleanSession=1 会清掉上次的订阅
    int okCount = 0;
    for (const auto& topic : subscribeTopics_) {
        const int subRc = mosquitto_subscribe(mosq_, nullptr, topic.c_str(), /*qos=*/0);
        if (subRc != MOSQ_ERR_SUCCESS) {
            LOG_ERROR(std::string("[MQTT] 订阅失败: ") + topic + ", " + mosquitto_strerror(subRc));
        } else {
            ++okCount;
        }
    }
    LOG_INFO("[MQTT] 已订阅 " + std::to_string(okCount) + "/"
             + std::to_string(subscribeTopics_.size()) + " 条: " + joinTopics(subscribeTopics_));
}

void MqttClient::handleMessage(const std::string& topic, const std::string& payload) {
    // 只认自己订阅的那几条（两种分隔符 × 三个 endpoint），防止串台
    const auto it = topicToEndpoint_.find(topic);
    if (it == topicToEndpoint_.end()) {
        LOG_DEBUG("[MQTT] 忽略未订阅 topic: " + topic);
        return;
    }
    if (payload.empty()) {
        LOG_WARNING("[MQTT] 收到空 payload，忽略, topic=" + topic);
        return;
    }
    if (handler_) {
        // 注意：这里是 mosquitto 的接收线程，业务处理不要阻塞太久，
        // 否则会拖慢心跳导致被 broker 断开。洗车命令本身都是毫秒级 PLC 写，没问题。
        handler_(it->second, payload);
    }
}
