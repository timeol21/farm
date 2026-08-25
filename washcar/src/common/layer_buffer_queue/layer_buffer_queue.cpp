#include "common/layer_buffer_queue/layer_buffer_queue.h"

LayerBufferQueue& LayerBufferQueue::instance()
{

    static LayerBufferQueue instance;

    return instance;

}

bool LayerBufferQueue::push(const CommandMessage& message)
{

    std::lock_guard<std::mutex> lock(mutex_);

    commandQueue_.push(message);

    return true;

}

bool LayerBufferQueue::pop(CommandMessage& message)
{

    std::lock_guard<std::mutex> lock(mutex_);

    if(commandQueue_.empty())
    {
        return false;
    }

    message = commandQueue_.front();

    commandQueue_.pop();

    return true;

}