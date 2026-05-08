#pragma once
#include "business_layer/command/mqtt/mqtt_object.h"
#include <vector>
#include <iostream>
//作用：1.编码：构建MQTT的 packet
//      2.解码：解析MQTT的 socket

class IMqttProtocol{
public:    
    virtual ~IMqttProtocol() = default;

    // encode 编码

    virtual std::vector<uint8_t> encodeConnect(const std::string& clientId) = 0;

    virtual std::vector<uint8_t> encodePublish(const std::string& topicconst, const std::string& payload) = 0;

    virtual std::vector<uint8_t> encodeSubscribe(const std::string& topic,uint16_t packetId) = 0;

    virtual std::vector<uint8_t> encodePing() = 0;

    virtual std::vector<uint8_t> encodeDisconnect() = 0;

    // ---------- decode ---------- 解码

    virtual bool decode(const std::vector<uint8_t>& buffer, MqttPacket& packet) = 0;

    virtual bool parseConnAck(const std::vector<uint8_t>& data) = 0;

};



class MqttProtocol : public IMqttProtocol{
public:
    std::vector<uint8_t> encodeConnect(const std::string& clientId) override;

    std::vector<uint8_t> encodePublish(const std::string& topicconst, const std::string& payload) override; // QoS0

    std::vector<uint8_t> encodeSubscribe(const std::string& topic, uint16_t packetId) override; // QoS1

    std::vector<uint8_t> encodePing() override;

    std::vector<uint8_t> encodeDisconnect() override;

    bool decode(const std::vector<uint8_t>& buffer,MqttPacket& packet) override;

    bool parseConnAck(const std::vector<uint8_t>& data) override;


private:
    static void encodeRemainingLength(std::vector<uint8_t>& buf, int length);



};