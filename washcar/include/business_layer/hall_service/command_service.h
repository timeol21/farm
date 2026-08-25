#pragma once
#include "common/message/command_message.h"
#include "common/layer_buffer_queue/layer_buffer_queue.h"

#include <queue>
#include <mutex>
#include <string>

class CommandService
{

public:

    bool initialize();

    bool start();

    void stop();
    
    void processCommand();

    bool popPendingCommand(CommandMessage& message);

private:

    void saveCommand(const CommandMessage& message);

    void enqueuePendingCommand(const CommandMessage& message);

    LayerBufferQueue& bufferQueue_ = LayerBufferQueue::instance();

    std::queue<CommandMessage> pendingCommands_;

    std::mutex pendingMutex_;

    bool running_ = false;


};