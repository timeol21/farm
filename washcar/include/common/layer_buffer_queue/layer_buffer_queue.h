#pragma once
#include "common/message/command_message.h"
#include <queue>
#include <mutex>
#include <string>


class LayerBufferQueue
{

public:

    LayerBufferQueue();

    ~LayerBufferQueue();

    bool push(const CommandMessage& message);

    bool pop(CommandMessage& message);

private:

    std::queue<CommandMessage> commandQueue_;

    std::mutex mutex_;
 

};