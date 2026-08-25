#pragma once

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

#include "common/layer_buffer_queue/layer_buffer_queue.h"

#include "common/message/command_message.h"

#include <thread>
#include <atomic>

class MqttParser
{

public:

    MqttParser(UserInterfaceQueue& inputQueue,LayerBufferQueue& outputQueue);
    
    ~MqttParser();

    /*
        从UI原始消息队列获取MQTT数据

        JSON解析

        转换成CommandMessage

        放入LayerBufferQueue

    */

    bool start();

    void stop();


private:

    /*
        MQTT消息解析

    */
    void run();

    bool process();

    bool parseMessage(const UserMessage& input,CommandMessage& output);

    UserInterfaceQueue& inputQueue_;

    LayerBufferQueue& outputQueue_;

    std::thread thread_;

    std::atomic<bool> running_ = false;

};
