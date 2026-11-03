#pragma once

// MQTT 客户端（基于 libmosquitto）
//
// 为什么用 mosquitto 而不是自己写协议:
//   原项目 mqtt_service.cpp + protocol.cpp 有 900 多行手写 MQTT 报文拼装/解析，
//   包含固定头、剩余长度变长编码、QoS、心跳、重连等。这类代码一旦出错极难排查。
//   libmosquitto 是 Eclipse 官方维护的成熟 C 库，把协议细节全部封装掉，
//   我们只需要 connect / subscribe / publish 三个动作。
//
// ★ 三条硬约束（云端 Flexem 平台要求，不能改）:
//   1. retain = false   —— 用 retain 会让云端收到陈旧状态
//   2. QoS   = 0        —— 平台只支持 QoS 0
//   3. cleanSession = 1 —— 每次连接都是干净会话，云端重投递由业务层保证
//
// 主题约定（见 docs/Flexem_MQTT_协议对接总结.md）:
//   发布  flexem{sep}{boxNo}{sep}{endpoint}        sep 由 mqtt.topicSeparator 决定，默认 "."
//   订阅  command / read / reboot 三个 endpoint，
//         **点号与斜杠两种写法都订阅**（生产抓包是点号，Excel 配置是斜杠）
//
// 本类只做"管道"，不认识业务字段:
//   收到的 topic 会被归一化成 endpoint（末段字符串）再交给上层，
//   上层（CommLayer 的回调）不需要关心分隔符是 "." 还是 "/"。
//   回调里只允许做一件事：把报文塞进下行队列。任何业务/PLC 操作都会阻塞
//   mosquitto 的收发线程（心跳、重连都在这个线程里）。

#include "common/config/config_object.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct mosquitto;

class MqttClient {
public:
    /// 收到下行消息的回调：(endpoint, payload)
    /// endpoint = "command" / "read" / "reboot"，已去掉前缀与分隔符。
    using MessageHandler = std::function<void(const std::string& endpoint, const std::string& payload)>;

    MqttClient();
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    /// 连接 broker 并开始后台收发线程。失败返回 false（内部会自动重连）。
    bool start(const washcar::cfg::MqttConfig& cfg, const std::string& boxNo, MessageHandler handler);

    /// 停止并释放
    void stop();

    bool isConnected() const { return connected_.load(); }

    /// 发布到 flexem{sep}{boxNo}{sep}{endpoint}。未连接时丢弃并返回 false（不阻塞业务）。
    bool publish(const std::string& endpoint, const std::string& payload);

    /// 按当前分隔符拼一条 topic（供日志/调试用）
    std::string buildTopic(const std::string& endpoint) const;

    /// 实际订阅的完整 topic 列表（启动日志会打成一行）
    const std::vector<std::string>& subscribeTopics() const { return subscribeTopics_; }

private:
    static void onConnectCb(struct mosquitto* m, void* obj, int rc);
    static void onMessageCb(struct mosquitto* m, void* obj, const struct mosquitto_message* msg);
    static void onDisconnectCb(struct mosquitto* m, void* obj, int rc);

    void handleConnect(int rc);
    void handleMessage(const std::string& topic, const std::string& payload);

    struct mosquitto* mosq_{nullptr};
    std::string boxNo_;
    char separator_{'.'};
    std::vector<std::string> subscribeTopics_;
    std::unordered_map<std::string, std::string> topicToEndpoint_;  // 完整 topic -> endpoint
    MessageHandler handler_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::mutex publishMutex_;

    // 重连策略，来自 config.json 的 mqtt 段：
    //   reconnectBaseSec  —— 指数退避的起始间隔（库内部每次翻倍）
    //   reconnectMaxRetry —— 连续失败多少次后开始报 ERROR（库本身会一直重试，这里只负责告警）
    int reconnectBaseSec_{2};
    int reconnectMaxRetry_{5};
    std::atomic<int> reconnectAttempts_{0};   // 连续失败次数，连上后清零
};
