#pragma once

#include <memory>

class HttpServer;

class MqttClient;

class TcpServer;

class RtspClient;

class MqttParser;

class UserInterfaceQueue;

class LayerBufferQueue;

class UserInterface
{

public:

    explicit UserInterface(LayerBufferQueue& layerBufferQueue);

    ~UserInterface();

    bool initialize();

    bool start();

    void stop();

    UserInterfaceQueue& userInterfaceQueue();

private:

    LayerBufferQueue& layerBufferQueue_;

    UserInterfaceQueue* userInterfaceQueue_ = nullptr;

    std::unique_ptr<HttpServer> httpServer_;

    std::unique_ptr<MqttClient> mqttClient_;

    std::unique_ptr<TcpServer> tcpServer_;

    std::unique_ptr<RtspClient> rtspClient_;

    std::unique_ptr<MqttParser> mqttParser_;

};