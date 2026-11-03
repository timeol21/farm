#pragma once

// 解析层：把收到的原始报文翻译成业务消息。
//
// 只做翻译，不做任何校验/写 PLC/发消息。解析失败返回 type=UNKNOWN 并且不抛异常，
// 上层拿到 UNKNOWN 就记日志丢弃即可。

#include "common/message.h"
#include "common/message.h"

#include <nlohmann/json.hpp>

#include <string>

class CommandParser {
public:
    /// boxType 决定用哪套命令方言（"haiTian" / "yiGongLi"）
    explicit CommandParser(const std::string& boxType);

    /// 解析一条下行报文。失败时 error 带原因，返回的 type 为 UNKNOWN。
    BusinessCommand parse(const RawMessage& msg, std::string& error) const;

private:
    /// command endpoint：按 payload 字段名判定是哪个命令
    CommandType classifyCommand(const nlohmann::json& payload) const;

    /// read endpoint：平铺 JSON 的键就是点位名，值忽略
    std::vector<std::string> extractRequestedPoints(const nlohmann::json& payload) const;

    std::string boxType_;
};
