#pragma once


#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

#include "common/layer_buffer_queue/layer_buffer_queue.h"

#include "common/message/command_message.h"



class HttpParser
{


public:


    HttpParser() = default;


    ~HttpParser() = default;

    /*
        HTTP请求解析

        HTTP JSON

        转换内部CommandMessage

    */
    bool process();

private:

    bool parseMessage(const UserMessage& input,CommandMessage& output);

    UserInterfaceQueue& inputQueue_ = UserInterfaceQueue::instance();
    
    LayerBufferQueue& outputQueue_ = LayerBufferQueue::instance();


};
