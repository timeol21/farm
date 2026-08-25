#pragma once

#include <string>

class UserInterfaceQueue;

class TcpServer
{

public:

    TcpServer() = default;

    ~TcpServer() = default;

    bool initialize(UserInterfaceQueue* UserInterfaceQueue);

    bool start();

    void stop();


private:

    void receiveData(int clientId,const std::string& data);

    void sendData();

    UserInterfaceQueue* userInterfaceQueue_ = nullptr;

    bool running_ = false;

};