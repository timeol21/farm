#pragma once

#include "common/config/config_object.h"
#include <string>
#include <vector>

// 把"逻辑功能"映射成实际 MQTT 主题,以及从收到的主题反查逻辑功能。
// 逻辑 key 固定为 7 个: monitor/readdata/read/pushalarm/command/commandreply/reboot。
// config.json 的 mqtt.topics 里可以任意改实际主题(斜杠/点号/自定义前缀),
// 业务代码只认逻辑 key,不再依赖 "flexem" 前缀或三段式解析。
class TopicResolver {
public:
    static std::string build(const washcar::cfg::MqttTopicsConfig& topics,
                             const std::string& boxNo,
                             const std::string& key);

    static bool resolve(const washcar::cfg::MqttTopicsConfig& topics,
                        const std::string& boxNo,
                        const std::string& topic,
                        std::string& key);

    static std::vector<std::pair<std::string, std::string>> all(
        const washcar::cfg::MqttTopicsConfig& topics,
        const std::string& boxNo);
};
