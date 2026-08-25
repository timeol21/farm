#include "userinterface_layer/mqtt/mqtt_connection.h"

#include <iostream>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")

#else

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#endif

MqttConnection::MqttConnection()
{

#ifdef _WIN32

    WSADATA wsa;

    WSAStartup(MAKEWORD(2,2),&wsa);

#endif

}

MqttConnection::~MqttConnection()
{

    disconnect();

    #ifdef _WIN32

    WSACleanup();

    #endif

}

bool MqttConnection::connect(const std::string& ip,int port)
{

    socketFd_ = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if(socketFd_ < 0)
    {
        return false;
    }

    sockaddr_in addr{};

    addr.sin_family = AF_INET;

    addr.sin_port = htons(port);

    if(inet_pton(AF_INET,ip.c_str(),&addr.sin_addr) <= 0)
    {
        disconnect();
        return false;
    }

    int result = ::connect(
        socketFd_,
        (sockaddr*)&addr,
        sizeof(addr)
    );

    if(result != 0)
    {

        disconnect();
        return false;

    }
    #ifdef _WIN32

    DWORD timeout = 1000;

    setsockopt(
        socketFd_,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char*)&timeout,
        sizeof(timeout)
    );

    #else

    timeval timeout;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    setsockopt(
        socketFd_,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );

    #endif

    connected_ = true;

    return true;

}

void MqttConnection::disconnect()
{

    if(socketFd_ != -1)
    {

        #ifdef _WIN32

            closesocket(socketFd_);

        #else

            close(socketFd_);

        #endif

        socketFd_ = -1;

    }

    connected_ = false;

}

bool MqttConnection::send(const std::vector<uint8_t>& data)
{

    if(!connected_)
    {
        return false;
    }

    size_t total = 0;

    while(total < data.size())
    {

        int size =::send(
                socketFd_,
                reinterpret_cast<const char*>(data.data()+total),
                data.size()-total,
                0
            );

        if(size <=0)
        {
            return false;
        }

        total += size;

    }

    return true;

}

bool MqttConnection::receive(std::vector<uint8_t>& data)
{

    if(!connected_)
    {
        return false;
    }

    uint8_t buffer[4096];

    int size =recv(
            socketFd_,
            reinterpret_cast<char*>(buffer),
            sizeof(buffer),
            0
        );

    if(size <=0)
    {
        return false;
    }

    data.assign(buffer,buffer+size);

    return true;

}

bool MqttConnection::connected() const
{

    return connected_;

}