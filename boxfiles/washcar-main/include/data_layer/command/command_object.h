#pragma once
#include "business_layer/command/command_object.h"
class Command;
enum class CommandType;
enum class CommandState;
struct CommandEntity{

    CommandEntity() = default;
    CommandEntity(const Command& cmd);

    ~CommandEntity() = default;
    
    static CommandEntity createEntity(const Command& cmd);
    std::string cmdId;         // 命令唯一ID
    std::string deviceId;      // 设备ID
    CommandType type;          // 命令类型
    CommandState state;        // 命令状态
    std::string content;       // 命令内容
    int64_t createTime;        // 创建时间戳
    int64_t updateTime;        // 更新时间戳



};