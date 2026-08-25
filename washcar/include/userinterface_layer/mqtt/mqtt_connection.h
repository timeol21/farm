#pragma once

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32

using SocketHandle = SOCKET;

#else

using SocketHandle = int;

#endif

class MqttConnection
{

public:

    MqttConnection();

    ~MqttConnection();

    /*
        建立TCP连接

        broker ip
        broker port

    */
    bool connect(const std::string& ip,int port);

    /*
        关闭连接

    */
    void disconnect();

    /*
        发送原始数据

        MQTT packet bytes

    */
    bool send(const std::vector<uint8_t>& data);

    /*
        接收原始数据

    */
    bool receive(std::vector<uint8_t>& data);

    /*
        判断连接状态

    */
    bool connected() const;

private:

    SocketHandle socketFd_ = -1;

    bool connected_ = false;

};