#pragma once

#include <memory>

class HttpServer;

class MqttClient;

class TcpServer;

class RtspClient;

class MqttParser;

class UserInterfaceQueue;

class UserInterface
{

public:

    static UserInterface& instance();

    bool initialize();

    bool start();

    void stop();

    UserInterfaceQueue& userInterfaceQueue();

private:
    UserInterface() = default;

    ~UserInterface() = default;

    UserInterface(const UserInterface&) = delete;

    UserInterface& operator=(const UserInterface&) = delete;

    UserInterfaceQueue* userInterfaceQueue_ = nullptr;

    std::unique_ptr<HttpServer> httpServer_;

    std::unique_ptr<MqttClient> mqttClient_;

    std::unique_ptr<TcpServer> tcpServer_;

    std::unique_ptr<RtspClient> rtspClient_;

    std::unique_ptr<MqttParser> mqttParser_;

};