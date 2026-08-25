#pragma once

#include <vector>
#include <string>
#include <cstdint>

enum class MqttPacketType
{
    CONNECT_ACK,
    PUBLISH,
    SUBSCRIBE_ACK,
    PING_RESPONSE
};

struct MqttPacket
{

    MqttPacketType type;

    std::string topic;

    std::string payload;

};

class MqttProtocol
{

public:

    MqttProtocol() = default;

    ~MqttProtocol() = default;

public:

    void setCredentials(const std::string& username,const std::string& password);

    std::vector<uint8_t> encodeConnect(const std::string& clientId);

    std::vector<uint8_t> encodePublish(const std::string& topic,const std::string& payload);

    std::vector<uint8_t> encodeSubscribe(const std::string& topic,uint16_t packetId);

    std::vector<uint8_t> encodePing();

    std::vector<uint8_t> encodeDisconnect();

    bool decode(const std::vector<uint8_t>& buffer,MqttPacket& packet);

    bool hasCompletePacket(const std::vector<uint8_t>& buffer);

    void removePacket(std::vector<uint8_t>& buffer);

private:

    void encodeRemainingLength(std::vector<uint8_t>& buffer,int length);

    std::string username_;

    std::string password_;

};