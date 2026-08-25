#include "userinterface_layer/mqtt/mqtt_protocol.h"

void MqttProtocol::setCredentials(const std::string& username,const std::string& password)
{

    username_ = username;
    password_ = password;

}

std::vector<uint8_t> MqttProtocol::encodeConnect(const std::string& clientId)
{

    std::vector<uint8_t> packet;

    packet.push_back(0x10);

    std::vector<uint8_t> body;

    body.push_back(0x00);
    body.push_back(0x04);

    body.push_back('M');
    body.push_back('Q');
    body.push_back('T');
    body.push_back('T');

    body.push_back(0x04);

    uint8_t flags = 0x02;

    if(!username_.empty())
    {
        flags |= 0x80;

        if(!password_.empty())
        {
            flags |= 0x40;
        }
    }

    body.push_back(flags);

    body.push_back(0x00);
    body.push_back(60);

    body.push_back((clientId.size() >> 8) & 0xff);

    body.push_back(clientId.size() & 0xff);

    body.insert(
        body.end(),
        clientId.begin(),
        clientId.end()
    );

    if(!username_.empty())
    {

        body.push_back((username_.size() >> 8) & 0xff);

        body.push_back(username_.size() & 0xff);

        body.insert(
            body.end(),
            username_.begin(),
            username_.end()
        );

    }

    if(!password_.empty())
    {

        body.push_back((password_.size() >> 8) & 0xff);

        body.push_back(password_.size() & 0xff);

        body.insert(
            body.end(),
            password_.begin(),
            password_.end()
        );

    }

    encodeRemainingLength(packet,body.size());

    packet.insert(
        packet.end(),
        body.begin(),
        body.end()
    );

    return packet;

}

std::vector<uint8_t> MqttProtocol::encodePublish(const std::string& topic,const std::string& payload)
{

    std::vector<uint8_t> packet;

    packet.push_back(0x30);

    std::vector<uint8_t> body;

    body.push_back((topic.size() >> 8) & 0xff);

    body.push_back(topic.size() & 0xff);

    body.insert(
        body.end(),
        topic.begin(),
        topic.end()
    );

    body.insert(
        body.end(),
        payload.begin(),
        payload.end()
    );

    encodeRemainingLength(packet,body.size());

    packet.insert(
        packet.end(),
        body.begin(),
        body.end()
    );

    return packet;

}

std::vector<uint8_t> MqttProtocol::encodeSubscribe(const std::string& topic,uint16_t packetId)
{

    std::vector<uint8_t> packet;

    packet.push_back(0x82);

    std::vector<uint8_t> body;

    body.push_back((packetId >> 8) & 0xFF);

    body.push_back(packetId & 0xFF);

    body.push_back((topic.size() >> 8) & 0xFF);

    body.push_back(topic.size() & 0xFF);

    body.insert(
        body.end(),
        topic.begin(),
        topic.end()
    );

    body.push_back(0x00);

    encodeRemainingLength(packet,body.size());

    packet.insert(
        packet.end(),
        body.begin(),
        body.end()
    );
    return packet;

}

std::vector<uint8_t> MqttProtocol::encodePing()
{

     return { 0xC0, 0x00 };

}

std::vector<uint8_t> MqttProtocol::encodeDisconnect()
{

    return { 0xE0,0x00 };

}

bool MqttProtocol::decode(const std::vector<uint8_t>& buffer,MqttPacket& packet
)
{
    if(buffer.size() < 2)
    {
        return false;
    }

    uint8_t type = buffer[0] >> 4;

    size_t index = 1;

    int remainingLength = 0;

    int multiplier = 1;

    uint8_t encodedByte;

    do
    {

        if(index >= buffer.size())
        {
            return false;
        }

        encodedByte = buffer[index++];

        remainingLength += (encodedByte & 127)*multiplier;

        multiplier *= 128;

    }
    while(encodedByte & 128);

    if(type == 3)
    {

        packet.type = MqttPacketType::PUBLISH;

        if(index + 2 > buffer.size())
        {
            return false;
        }

        uint16_t topicLength =(buffer[index] << 8)| buffer[index+1];

        index += 2;

        if(index + topicLength > buffer.size())
        {
            return false;
        }

        packet.topic = std::string(
                buffer.begin()+index,
                buffer.begin()+index+topicLength
        );

        index += topicLength;

        size_t packetEnd = index + remainingLength - topicLength - 2;

        packet.payload = std::string(
            buffer.begin()+index,
            buffer.begin()+packetEnd
        );

        return true;

    }

    if(type == 2)
    {

        packet.type = MqttPacketType::CONNECT_ACK;

        return true;

    }

    if(type == 9)
    {
        packet.type = MqttPacketType::SUBSCRIBE_ACK;
        return true;

    }

    if(type == 13)
    {
        packet.type = MqttPacketType::PING_RESPONSE;
        return true;
    }

    return false;

}

void MqttProtocol::encodeRemainingLength(std::vector<uint8_t>& buffer,int length)
{

    do
    {

        uint8_t byte = length % 128;

        length /= 128;

        if(length > 0)
        {
            byte |= 0x80;
        }

        buffer.push_back(byte);

    }
    while(length > 0);

}

bool MqttProtocol::hasCompletePacket(
    const std::vector<uint8_t>& buffer
)
{

    if(buffer.size() < 2)
    {
        return false;
    }


    size_t index = 1;


    int remainingLength = 0;

    int multiplier = 1;


    uint8_t byte;


    do
    {

        if(index >= buffer.size())
        {
            return false;
        }


        byte = buffer[index++];


        remainingLength +=
            (byte & 127) * multiplier;


        multiplier *= 128;


    }
    while(byte & 128);



    size_t packetLength =
        index + remainingLength;



    return buffer.size() >= packetLength;

}

void MqttProtocol::removePacket(std::vector<uint8_t>& buffer)
{

    if(buffer.size() < 2)
    {
        return;
    }

    size_t index = 1;

    int remainingLength = 0;

    int multiplier = 1;

    uint8_t byte;

    do
    {
        if(index >= buffer.size())
        {
            return;
        }
        byte = buffer[index++];

        remainingLength += (byte & 127) * multiplier;

        multiplier *= 128;

    }
    while(byte & 128);

    size_t packetLength = index + remainingLength;

    if(buffer.size() >= packetLength)
    {

        buffer.erase(
            buffer.begin(),
            buffer.begin() + packetLength
        );

    }

}