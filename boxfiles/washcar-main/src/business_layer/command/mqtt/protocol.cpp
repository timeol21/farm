#include "business_layer/command/mqtt/protocol.h"

std::vector<uint8_t> MqttProtocol::encodeConnect(const std::string& clientId) {
    std::vector<uint8_t> packet;
    packet.push_back(0x10);
    std::vector<uint8_t> body;
    // 协议名 "MQTT"
    body.push_back(0x00); body.push_back(0x04);
    body.push_back('M'); body.push_back('Q'); body.push_back('T'); body.push_back('T');
    body.push_back(0x04); // Level 4
    
    // Connect Flags (包含用户名密码标志)
    uint8_t flags = 0x02; // Clean Session
    if (!username_.empty()) {
        flags |= 0x80; // User Name Flag
        if (!password_.empty()) {
            flags |= 0x40; // Password Flag
        }
    }
    body.push_back(flags);
    
    // Keep Alive
    body.push_back(0x00); body.push_back(60);
    
    // Client ID
    body.push_back((clientId.size() >> 8) & 0xFF);
    body.push_back(clientId.size() & 0xFF);
    body.insert(body.end(), clientId.begin(), clientId.end());
    
    // Username
    if (!username_.empty()) {
        body.push_back((username_.size() >> 8) & 0xFF);
        body.push_back(username_.size() & 0xFF);
        body.insert(body.end(), username_.begin(), username_.end());
    }
    
    // Password
    if (!password_.empty()) {
        body.push_back((password_.size() >> 8) & 0xFF);
        body.push_back(password_.size() & 0xFF);
        body.insert(body.end(), password_.begin(), password_.end());
    }
    
    encodeRemainingLength(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}
// std::vector<uint8_t> MqttProtocol::encodeConnect(const std::string& clientId){
//     std::vector<uint8_t> packet;
//     packet.push_back(0x10); // CONNECT packet type

//     std::vector<uint8_t> body;

//     body.push_back(0x00); // Protocol Name Length MSB
//     body.push_back(0x04); // Protocol Name Length LSB

//     body.push_back('M');  // Protocol Name 'M'
//     body.push_back('Q');  // Protocol Name 'Q'
//     body.push_back('T');  // Protocol Name 'T'
//     body.push_back('T');  // Protocol Name 'T'

//     body.push_back(0x04); // Protocol Level
//     body.push_back(0x02); // Connect Flags (Clean Session)

//     body.push_back(0x00); // Keep Alive MSB
//     body.push_back(60); // Keep Alive LSB (60 seconds)

//     // Payload: Client ID
//     body.push_back((clientId.size() >> 8) & 0xFF);

//     body.push_back(clientId.size() & 0xFF);

//     body.insert(body.end(), clientId.begin(), clientId.end());

//     encodeRemainingLength(packet, body.size());

//     packet.insert(packet.end(), body.begin(), body.end());

//     return packet;

// }

std::vector<uint8_t> MqttProtocol::encodePublish(const std::string& top ,const std::string& payload){
    std::vector<uint8_t> packet;
    packet.push_back(0x30); // PUBLISH packet type (QoS 0)
    std::vector<uint8_t> body;
    // Topic
    body.push_back((top.size() >> 8) & 0xFF);
    body.push_back(top.size() & 0xFF);
    body.insert(body.end(), top.begin(), top.end());

    // Payload
    body.insert(body.end(), payload.begin(), payload.end());
    
    encodeRemainingLength(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());

    return packet;
}

std::vector<uint8_t> MqttProtocol::encodeSubscribe(const std::string& topic,uint16_t packetId){
    std::vector<uint8_t> packet;
    packet.push_back(0x82); // SUBSCRIBE packet type (QoS 1)

    std::vector<uint8_t> body;

    // Packet ID
    body.push_back((packetId >> 8) & 0xFF);
    body.push_back(packetId & 0xFF);

    // Topic Filter
    body.push_back((topic.size() >> 8) & 0xFF);
    body.push_back(topic.size() & 0xFF);
    body.insert(body.end(), topic.begin(), topic.end());

    // QoS
    body.push_back(0x00); // QoS 0

    encodeRemainingLength(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());

    return packet;
}

std::vector<uint8_t> MqttProtocol::encodePing(){
    return {0xC0, 0x00}; // PINGREQ packet
}

std::vector<uint8_t> MqttProtocol::encodeDisconnect(){
    return {0xE0,0x00};
}

bool MqttProtocol::decode(const std::vector<uint8_t>& buffer,MqttPacket& packet){
    if(buffer.size() < 2) return false;

    uint8_t type = buffer[0] >> 4;

    size_t index = 1;

    int remainingLength = 0;
    int multiplier = 1;

    uint8_t encodeByte;

    do{
        if(index >= buffer.size()) return false;

        encodeByte = buffer[index++];

        remainingLength += (encodeByte & 127) * multiplier;

        multiplier *= 128;

    } while(encodeByte & 128);

    if(type == 3) //publish
    {
        packet.type = MqttPacketType::PUBLISH;

        if(index + 2 > buffer.size()) return false;

        uint16_t topicLength = (buffer[index] << 8) | buffer[index + 1];

        index += 2;

        if(index + topicLength > buffer.size()) return false;

        packet.topic = std::string(buffer.begin() + index, buffer.begin() + index + topicLength);

        index += topicLength;

        if(index > buffer.size()) return false;

        packet.payload = std::string(buffer.begin() + index, buffer.end());

    } else {
        packet.type = static_cast<MqttPacketType>(type);
    }
    
    return true;
}

bool MqttProtocol::parseConnAck(const std::vector<uint8_t>& data) {
     std::cout << "parseConnAck: data size = " << data.size() << std::endl;
    if (data.size() < 2) {
        std::cerr << "CONNACK too short (less than 2 bytes)" << std::endl;
        return false;
    }
    
    uint8_t packetType = data[0] >> 4;
    std::cout << "Packet type: " << (int)packetType << std::endl;
    if (packetType != 2) {
        std::cerr << "Not a CONNACK packet" << std::endl;
        return false;
    }
    
    size_t pos = 1;
    int remainingLength = 0;
    int multiplier = 1;
    uint8_t encodedByte;
    do {
        if (pos >= data.size()) {
            std::cerr << "Incomplete remaining length" << std::endl;
            return false;
        }
        encodedByte = data[pos++];
        remainingLength += (encodedByte & 127) * multiplier;
        multiplier *= 128;
    } while ((encodedByte & 128) && multiplier <= 128*128*128);
    
    std::cout << "Remaining length = " << remainingLength << std::endl;
    
    // CONNACK 固定剩余长度为 2，检查是否足够
    if (data.size() < pos + 2) {
        std::cerr << "CONNACK too short, need " << (pos+2) << " bytes, got " << data.size() << std::endl;
        return false;
    }
    
    uint8_t connectAckFlags = data[pos];
    uint8_t returnCode = data[pos + 1];
    std::cout << "Connect Acknowledge Flags: " << (int)connectAckFlags << ", Return Code: " << (int)returnCode << std::endl;
    
    if (returnCode == 0) {
        std::cout << "MQTT connected successfully" << std::endl;
        return true;
    } else {
        std::cerr << "MQTT connect failed, return code: " << (int)returnCode << std::endl;
        return false;
    }
}
// bool MqttProtocol::parseConnAck(const std::vector<uint8_t>& data) {
//     if (data.size() < 4) {
//         std::cerr << "CONNACK too short\n";
//         return false;
//     }

//     uint8_t packetType = data[0] >> 4;
//     if (packetType != 2) { // 2 = CONNACK
//         std::cerr << "Not CONNACK\n";
//         return false;
//     }

//     uint8_t returnCode = data[3];

//     if (returnCode == 0) {
//         std::cout << "MQTT connected successfully\n";
//         return true;
//     } else {
//         std::cerr << "MQTT connect failed, code: " << (int)returnCode << "\n";
//         return false;
//     }
// }
// private
void MqttProtocol::setCredentials(const std::string& username, const std::string& password) {
    username_ = username;
    password_ = password;
}


void MqttProtocol::encodeRemainingLength(std::vector<uint8_t>& buf, int length){

    do{

        uint8_t encodeByte = length % 128;
        
        length /= 128;

        if(length > 0) encodeByte |= 128;

        buf.push_back(encodeByte);

    } while(length > 0);


}