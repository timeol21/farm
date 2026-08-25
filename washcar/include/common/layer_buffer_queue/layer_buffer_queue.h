#pragma once
#include "common/message/command_message.h"
#include <queue>
#include <mutex>
#include <string>


class LayerBufferQueue
{

public:

    static LayerBufferQueue& instance();

    bool push(const CommandMessage& message);

    bool pop(CommandMessage& message);

private:

    LayerBufferQueue() = default;

    ~LayerBufferQueue() = default;

    LayerBufferQueue(const LayerBufferQueue&) = delete;

    LayerBufferQueue& operator=(const LayerBufferQueue&) = delete;

    std::queue<CommandMessage> commandQueue_;

    std::mutex mutex_;
 

};