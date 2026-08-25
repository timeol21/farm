#pragma once

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

#include "common/layer_buffer_queue/layer_buffer_queue.h"

#include "common/message/command_message.h"

class TcpParser
{

public:

    TcpParser() = default;

    ~TcpParser() = default;

    /*
        TCP协议解析

        转换内部命令

    */
    bool process();

private:

    bool parseMessage(const UserMessage& input,CommandMessage& output);

    UserInterfaceQueue& inputQueue_ = UserInterfaceQueue::instance();

    LayerBufferQueue& outputQueue_ = LayerBufferQueue::instance();


};
