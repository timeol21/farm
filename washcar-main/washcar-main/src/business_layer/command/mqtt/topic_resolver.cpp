#include "business_layer/command/mqtt/topic_resolver.h"

#include <algorithm>

namespace {

const char* const kKeys[] = {
    "monitor", "readdata", "read", "pushalarm", "command", "commandreply", "reboot"
};

const std::string* fieldOf(const washcar::cfg::MqttTopicsConfig& t, const std::string& key) {
    if (key == "monitor")      return &t.monitor;
    if (key == "readdata")     return &t.readdata;
    if (key == "read")         return &t.read;
    if (key == "pushalarm")    return &t.pushalarm;
    if (key == "command")      return &t.command;
    if (key == "commandreply") return &t.commandreply;
    if (key == "reboot")       return &t.reboot;
    return nullptr;
}

std::string substituteBoxNo(std::string topic, const std::string& boxNo) {
    const std::string placeholder = "{boxNo}";
    std::size_t pos = 0;
    while ((pos = topic.find(placeholder, pos)) != std::string::npos) {
        topic.replace(pos, placeholder.size(), boxNo);
        pos += boxNo.size();
    }
    return topic;
}

}  // namespace

std::string TopicResolver::build(const washcar::cfg::MqttTopicsConfig& topics,
                                 const std::string& boxNo,
                                 const std::string& key) {
    if (const std::string* field = fieldOf(topics, key)) {
        return substituteBoxNo(*field, boxNo);
    }
    // 兼容旧调用:有些地方传的是 "event/post" 这类逻辑后缀,不是 7 个业务 key。
    // 保持改造前的 "flexem/boxNo/suffix" 行为,避免老测试/老调用断掉。
    return "flexem/" + boxNo + "/" + key;
}

bool TopicResolver::resolve(const washcar::cfg::MqttTopicsConfig& topics,
                            const std::string& boxNo,
                            const std::string& topic,
                            std::string& key) {
    for (const char* candidate : kKeys) {
        if (build(topics, boxNo, candidate) == topic) {
            key = candidate;
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> TopicResolver::all(
    const washcar::cfg::MqttTopicsConfig& topics,
    const std::string& boxNo) {
    std::vector<std::pair<std::string, std::string>> result;
    for (const char* key : kKeys) {
        result.push_back({key, build(topics, boxNo, key)});
    }
    return result;
}
