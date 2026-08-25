#pragma once   // 自定义的内部消息类型

#include <string>


enum class CommandType
{

    NONE,

    START_WASH,

    STOP_WASH,

    PAUSE_WASH,

    RESET_DEVICE,

    QUERY_STATUS

};



struct CommandMessage
{

    CommandType type =CommandType::NONE;

    std::string deviceId;

    std::string parameter;

    std::string source;

    uint64_t timestamp;
};
