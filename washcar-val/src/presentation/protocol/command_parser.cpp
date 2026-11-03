#include "presentation/protocol/command_parser.h"

#include "common/config/point.h"

#include <set>
#include <string>

namespace {

using washcar::cfg::formatPoint;
using washcar::cfg::parsePoint;

/// 云端命令里"值是 1 才算触发"。容错处理字符串 "1" 和 true，
/// 因为 Excel 导出的 payload 里数字有时被写成字符串。
bool isFlagOn(const nlohmann::json& payload, const char* key) {
    const auto it = payload.find(key);
    if (it == payload.end()) {
        return false;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number_integer()) {
        return it->get<int>() == 1;
    }
    if (it->is_string()) {
        return it->get<std::string>() == "1";
    }
    return false;
}

/// read payload 里不是点位的键
bool isMetadataKey(const std::string& key) {
    static const std::set<std::string> kKeys = {
        "boxNo", "plcId", "time", "mode", "reqSource", "productId", "deviceNum"
    };
    return kKeys.count(key) > 0;
}

}  // namespace

const char* toString(CommandType type) {
    switch (type) {
    case CommandType::START:         return "START";
    case CommandType::STOP:          return "STOP";
    case CommandType::RESET:         return "RESET";
    case CommandType::ABORT:         return "ABORT";
    case CommandType::STOP_WORK:     return "STOP_WORK";
    case CommandType::STOP_SERVICE:  return "STOP_SERVICE";
    case CommandType::RESET_SAFE:    return "RESET_SAFE";
    case CommandType::READ:          return "READ";
    case CommandType::REBOOT:        return "REBOOT";
    case CommandType::POINT_ON:      return "POINT_ON";
    case CommandType::POINT_OFF:     return "POINT_OFF";
    case CommandType::POINT_STATUS:  return "POINT_STATUS";
    case CommandType::CHANGE_MODE:   return "CHANGE_MODE";
    case CommandType::CLEAR_ALARM:   return "CLEAR_ALARM";
    case CommandType::UNKNOWN:       break;
    }
    return "UNKNOWN";
}

CommandParser::CommandParser(const std::string& boxType) : boxType_(boxType) {}

BusinessCommand CommandParser::parse(const RawMessage& msg, std::string& error) const {
    BusinessCommand cmd;
    cmd.source = msg.endpoint;
    error.clear();

    if (msg.payload.empty()) {
        error = "empty payload";
        return cmd;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(msg.payload);
    } catch (const std::exception& e) {
        error = std::string("json parse error: ") + e.what();
        return cmd;
    }

    if (!payload.is_object()) {
        error = "payload is not a json object";
        return cmd;
    }

    if (msg.endpoint == "command") {
        cmd.type = classifyCommand(payload);
        if (cmd.type == CommandType::UNKNOWN) {
            error = "no recognized command field";
        }
    } else if (msg.endpoint == "read") {
        cmd.type = CommandType::READ;
        cmd.points = extractRequestedPoints(payload);
        if (cmd.points.empty()) {
            error = "read request carries no point name";
        }
    } else if (msg.endpoint == "reboot") {
        cmd.type = CommandType::REBOOT;
    } else {
        error = "unsupported endpoint: " + msg.endpoint;
    }

    cmd.raw = std::move(payload);
    return cmd;
}

CommandType CommandParser::classifyCommand(const nlohmann::json& payload) const {
    // 启动字段两个机型一致
    if (isFlagOn(payload, "Manual start")) {
        return CommandType::START;
    }

    if (boxType_ == "yiGongLi") {
        if (isFlagOn(payload, "washcar stop"))  return CommandType::STOP;
        if (isFlagOn(payload, "machine abort")) return CommandType::ABORT;
        if (isFlagOn(payload, "reset safely"))  return CommandType::RESET_SAFE;
        if (isFlagOn(payload, "reset"))         return CommandType::RESET;
        if (isFlagOn(payload, "stop work"))     return CommandType::STOP_WORK;
        if (isFlagOn(payload, "stop service"))  return CommandType::STOP_SERVICE;
        // 兼容：少数场景云端直接发 stop
        if (isFlagOn(payload, "stop"))          return CommandType::STOP;
        return CommandType::UNKNOWN;
    }

    if (isFlagOn(payload, "stop"))  return CommandType::STOP;
    if (isFlagOn(payload, "reset")) return CommandType::RESET;
    return CommandType::UNKNOWN;
}

std::vector<std::string> CommandParser::extractRequestedPoints(const nlohmann::json& payload) const {
    std::vector<std::string> points;
    points.reserve(payload.size());

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        const std::string key = it.key();
        if (isMetadataKey(key)) {
            continue;
        }
        const auto parsed = parsePoint(key);
        if (parsed.type == '?') {
            continue;   // 不是软元件名，忽略
        }
        points.push_back(formatPoint(parsed.type, parsed.addr));
    }
    return points;
}
