#pragma once
#include <string>
#include <cstdint>

enum class MqttPacketType{ // 构建mqtt发送消息的内容
    UNKNOWN = 0,
    CONNECT = 1, // 方向:客户端→服务端(bocket)  作用:请求建立连接
    CONNACK = 2, // 服务端→客户端 连接请求的响应
    PUBLISH = 3, // 双向  发布消息
    PUBACK = 4, // 双向  发布消息的响应
    SUBSCRIBE = 8, // 客户端→服务端  请求订阅主题
    SUBACK = 9, // 服务端→客户端  订阅请求的响应
    PINGREQ = 12, // 客户端→服务端  心跳请求
    PINGRESP = 13, // 服务端→客户端  心跳响应
    DISCONNECT = 14 // 客户端→服务端  断开连接请求
};

struct  MqttPacket
{
    MqttPacketType type = MqttPacketType::UNKNOWN;
    std::string topic; // 主题
    std::string payload; // 消息内容
    uint16_t packetId = 0; // 包ID（仅PUBLISH、PUBACK、SUBSCRIBE、SUBACK需要）
};

