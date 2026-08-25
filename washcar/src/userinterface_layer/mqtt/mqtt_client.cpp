#include "userinterface_layer/mqtt/mqtt_client.h"

#include "userinterface_layer/mqtt/mqtt_protocol.h"
#include "userinterface_layer/mqtt/mqtt_connection.h"

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"


#include <chrono>
#include <iostream>

MqttClient::MqttClient()
{

}

bool MqttClient::initialize(UserInterfaceQueue* userInterfaceQueue)
{

    if(userInterfaceQueue == nullptr)
    {
        return false;
    }

    userInterfaceQueue_ = userInterfaceQueue;

    protocol_ = std::make_unique<MqttProtocol>();

    connection_ = std::make_unique<MqttConnection>();

    protocol_->setCredentials(
        "admin",
        "llx@123456"
    );

    lastPingTime_ = std::chrono::steady_clock::now();

    return true;

}

bool MqttClient::connectBroker()
{

    if(!connection_)
    {
        return false;
    }

    bool result = connection_->connect(brokerIp_,brokerPort_);

    if(!result)
    {
        return false;
    }

    auto packet =protocol_->encodeConnect(clientId_);

    if(!connection_->send(packet))
    {
        return false;
    }

    // std::vector<uint8_t> buffer;

    // if(connection_->receive(buffer))
    // {

    //     MqttPacket mqttPacket;

    //     if(protocol_->decode(buffer,mqttPacket))
    //     {

    //         if(mqttPacket.type == MqttPacketType::CONNECT_ACK)
    //         {

    //             if(!subscribeTopics())
    //             {
    //                 return false;
    //             }

    //             connected_ = true;

    //             return true;
    //         }

    //     }

    // }
    MqttPacket connAck;

    if(!waitPacket(MqttPacketType::CONNECT_ACK,connAck))
    {
        return false;
    }

    if(!subscribeTopics())
    {
        return false;
    }
    connected_=true;

    return true;

}

bool MqttClient::start()
{

    if(running_)
    {
        return true;
    }

    if(!connected_)
    {
        if(!connectBroker())
        {
            return false;
        }
    }

    running_ = true;

    thread_ = std::thread(&MqttClient::run,this);

    return true;

}

void MqttClient::run()
{

    while(running_)
    {

        std::vector<uint8_t> buffer;

        // TCP读取
        if(connection_->receive(buffer))
        {

            // TCP数据进入缓存
            receiveBuffer_.insert(
                receiveBuffer_.end(),
                buffer.begin(),
                buffer.end()
            );

            // 一个TCP里面可能多个MQTT包
            while(protocol_->hasCompletePacket(receiveBuffer_))
            {

                MqttPacket packet;

                if(protocol_->decode(receiveBuffer_,packet))
                {

                    handlePacket(packet);

                }

                protocol_->removePacket(receiveBuffer_);

            }

        }

        auto now = std::chrono::steady_clock::now();

        if(std::chrono::duration_cast<std::chrono::seconds>(now - lastPingTime_).count()> 30)
        {

            auto ping = protocol_->encodePing();

            connection_->send(ping);

            lastPingTime_ = now;

        }

        processSendQueue();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    }

}


void MqttClient::stop()
{

    running_ = false;

    if(thread_.joinable())
    {
        thread_.join();
    }

    if(connection_)
    {
        connection_->disconnect();
    }

    connected_ = false;

}

void MqttClient::handlePacket(const MqttPacket& packet)
{

    switch(packet.type)
    {

        case MqttPacketType::PUBLISH:
        {

            onMessage(
                packet.topic,
                packet.payload
            );

            break;
        }

        case MqttPacketType::PING_RESPONSE:
        {

            break;
        }

        case MqttPacketType::SUBSCRIBE_ACK:
        {

            break;
        }

        case MqttPacketType::CONNECT_ACK:
        {

            break;
        }

        default:
        {
            break;
        }

    }

}


void MqttClient::onMessage(const std::string& topic,const std::string& payload)
{

    if(userInterfaceQueue_ == nullptr)
    {
        return;
    }

    UserMessage message;

    message.data = payload;

    /*
        MQTT收到原始消息
        放入:
        UserInterfaceQueue
        后续:
        MqttParser
        从这里读取
    */
    userInterfaceQueue_->pushMqttReceive(message);

}

void MqttClient::processSendQueue()
{

    if(userInterfaceQueue_ == nullptr)
    {
        return;
    }

    UserMessage message;

    while(userInterfaceQueue_->popMqttSend(message))
    {

        auto packet =protocol_->encodePublish("box/" + clientId_ + "/report",message.data);

        connection_->send(packet);

    }

}

bool MqttClient::subscribeTopics()
{

    auto subscribePacket = protocol_->encodeSubscribe("box/" + clientId_ + "/command",1);

    if(!connection_->send(subscribePacket))
    {
        return false;
    }

    /*
        等待SUBACK
    */

    std::vector<uint8_t> buffer;

    if(!connection_->receive(buffer))
    {
        return false;
    }

    MqttPacket packet;

    if(!protocol_->decode(buffer,packet))
    {
        return false;
    }

    if(packet.type != MqttPacketType::SUBSCRIBE_ACK)
    {
        return false;
    }

    return true;

}

bool MqttClient::waitPacket(MqttPacketType type,MqttPacket& packet)
{

    while(connection_->connected())
    {

        std::vector<uint8_t> buffer;

        if(connection_->receive(buffer))
        {

            receiveBuffer_.insert(
                receiveBuffer_.end(),
                buffer.begin(),
                buffer.end()
            );

            while(
                protocol_->hasCompletePacket(receiveBuffer_)
            )
            {

                MqttPacket temp;

                if(protocol_->decode(receiveBuffer_,temp))
                {

                    protocol_->removePacket(receiveBuffer_);

                    if(temp.type == type)
                    {

                        packet = temp;

                        return true;

                    }

                }
                else
                {

                    protocol_->removePacket(receiveBuffer_);

                }

            }

        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );

    }

    return false;

}

MqttClient::~MqttClient()
{
    stop();
}