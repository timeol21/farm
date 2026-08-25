#include "business_layer/hall_service/command_service.h"

#include <iostream>

CommandService::CommandService(LayerBufferQueue& bufferQueue)
:
bufferQueue_(bufferQueue)
{

}

bool CommandService::initialize()
{

    return true;

}

bool CommandService::start()
{

    running_ = true;

    return true;

}

void CommandService::stop()
{

    running_ = false;

}

void CommandService::processCommand()
{

    if(!running_)
    {
        return;
    }

    CommandMessage message;

    while(bufferQueue_.pop(message))
    {

        saveCommand(message);

        enqueuePendingCommand(message);

    }

}

void CommandService::saveCommand(const CommandMessage& message)
{
    /*
        后续这里接数据库

        例如:

        DatabaseManager

        保存:
        - 命令类型
        - 来源
        - 时间
        - 设备ID

    */

    std::cout << "Save command: " << static_cast<int>(message.type) << std::endl;

}

void CommandService::enqueuePendingCommand(const CommandMessage& message)
{
    /*
        当前阶段:
        放入待执行命令列表
        后续:
        可以由:
        WashService
        或
        状态机
        消费

    */

    std::lock_guard<std::mutex> lock(pendingMutex_);

    pendingCommands_.push(message);

}

bool CommandService::popPendingCommand(CommandMessage& message)
{

    std::lock_guard<std::mutex> lock(pendingMutex_);

    if(pendingCommands_.empty())
    {
        return false;
    }

    message = pendingCommands_.front();

    pendingCommands_.pop();

    return true;

}