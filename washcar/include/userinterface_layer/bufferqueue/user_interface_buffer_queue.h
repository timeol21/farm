#pragma once

#include <queue>
#include <mutex>
#include <string>

struct UserMessage
{

    std::string data;

};


class UserInterfaceQueue
{

public:

    static UserInterfaceQueue& instance();

public:

    bool pushMqttReceive(const UserMessage& message);

    bool popMqttReceive(UserMessage& message);

    bool pushMqttSend(const UserMessage& message);

    bool popMqttSend(UserMessage& message);

    bool pushHttpReceive(const UserMessage& message);

    bool popHttpReceive(UserMessage& message);

    bool pushHttpSend(const UserMessage& message);

    bool popHttpSend(UserMessage& message);

    bool pushTcpReceive(const UserMessage& message);

    bool popTcpReceive(UserMessage& message);

    bool pushTcpSend(const UserMessage& message);

    bool popTcpSend(UserMessage& message);


private:

    UserInterfaceQueue() = default;

    ~UserInterfaceQueue() = default;

    UserInterfaceQueue(const UserInterfaceQueue&) = delete;

    UserInterfaceQueue& operator=(const UserInterfaceQueue&) = delete;


private:

    std::queue<UserMessage> mqttReceiveQueue_;

    std::queue<UserMessage> mqttSendQueue_;

    std::queue<UserMessage> httpReceiveQueue_;

    std::queue<UserMessage> httpSendQueue_;

    std::queue<UserMessage> tcpReceiveQueue_;

    std::queue<UserMessage> tcpSendQueue_;

    std::mutex mqttReceiveMutex_;

    std::mutex mqttSendMutex_;

    std::mutex httpReceiveMutex_;

    std::mutex httpSendMutex_;

    std::mutex tcpReceiveMutex_;

    std::mutex tcpSendMutex_;

};