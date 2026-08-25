#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

#include "userinterface_layer/mqtt/mqtt_protocol.h"

class UserInterfaceQueue;

class MqttProtocol;

class MqttConnection;

struct MqttPacket;

class MqttClient
{

public:

    MqttClient();

    ~MqttClient();

    bool initialize(UserInterfaceQueue* userInterfaceQueue);

    bool connectBroker();

    bool start();

    void stop();

    void onMessage(const std::string& topic,const std::string& payload);

    void processSendQueue();


private:

    void run();
 
    bool subscribeTopics();

    void handlePacket(const MqttPacket& packet);

    bool waitPacket(MqttPacketType type,MqttPacket& packet);

    UserInterfaceQueue* userInterfaceQueue_ = nullptr;

    std::unique_ptr<MqttProtocol> protocol_;

    std::unique_ptr<MqttConnection> connection_;

    std::vector<uint8_t> receiveBuffer_;

    std::string clientId_ = "123456";

    std::string brokerIp_ = "116.204.32.197";

    int brokerPort_ = 1883;

    std::thread thread_;

    std::atomic<bool> running_ = false;

    bool connected_ = false;

    std::chrono::steady_clock::time_point lastPingTime_;

};