#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

UserInterfaceQueue& UserInterfaceQueue::instance()
{

    static UserInterfaceQueue instance;

    return instance;

}

bool UserInterfaceQueue::pushMqttReceive(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(mqttReceiveMutex_);

    mqttReceiveQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popMqttReceive(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(mqttReceiveMutex_);
    if(mqttReceiveQueue_.empty())
    {
        return false;
    }

    message = mqttReceiveQueue_.front();

    mqttReceiveQueue_.pop();

    return true;

}

bool UserInterfaceQueue::pushMqttSend(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(mqttSendMutex_);

    mqttSendQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popMqttSend(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(mqttSendMutex_);

    if(mqttSendQueue_.empty())
    {
        return false;
    }

    message = mqttSendQueue_.front();

    mqttSendQueue_.pop();

    return true;

}

bool UserInterfaceQueue::pushHttpReceive(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(httpReceiveMutex_);

    httpReceiveQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popHttpReceive(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(httpReceiveMutex_);

    if(httpReceiveQueue_.empty())
    {
        return false;
    }

    message = httpReceiveQueue_.front();

    httpReceiveQueue_.pop();

    return true;

}

bool UserInterfaceQueue::pushHttpSend(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(httpSendMutex_);

    httpSendQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popHttpSend(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(httpSendMutex_);

    if(httpSendQueue_.empty())
    {
        return false;
    }

    message = httpSendQueue_.front();

    httpSendQueue_.pop();

    return true;

}

bool UserInterfaceQueue::pushTcpReceive(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(tcpReceiveMutex_);

    tcpReceiveQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popTcpReceive(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(tcpReceiveMutex_);

    if(tcpReceiveQueue_.empty())
    {
        return false;
    }

    message = tcpReceiveQueue_.front();

    tcpReceiveQueue_.pop();

    return true;

}

bool UserInterfaceQueue::pushTcpSend(const UserMessage& message)
{

    std::lock_guard<std::mutex> lock(tcpSendMutex_);

    tcpSendQueue_.push(message);

    return true;

}

bool UserInterfaceQueue::popTcpSend(UserMessage& message)
{

    std::lock_guard<std::mutex> lock(tcpSendMutex_);

    if(tcpSendQueue_.empty())
    {
        return false;
    }

    message = tcpSendQueue_.front();

    tcpSendQueue_.pop();

    return true;

}