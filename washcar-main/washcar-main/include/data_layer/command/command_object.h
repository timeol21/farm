#pragma once
#include "business_layer/command/command_object.h"

// 命令台账实体: 只留落库要存的列。
// 命令的"状态"没有使用场景(没有超时/补发状态机), 已从模型里去掉; 表里的 state 列保留为 0。
struct CommandEntity {

    CommandEntity() = default;
    CommandEntity(Command::Type type, const std::string& deviceId);
    ~CommandEntity() = default;

    std::string cmdId;
    std::string deviceId;
    Command::Type type = Command::Type::OTHER;
    std::string content;
    int64_t createTime = 0;
    int64_t updateTime = 0;
};
