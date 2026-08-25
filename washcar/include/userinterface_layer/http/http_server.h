#pragma once

#include <string>

class UserInterfaceQueue;


class HttpServer
{

public:

    HttpServer() = default;

    ~HttpServer() = default;


    bool initialize(UserInterfaceQueue* UserInterfaceQueue);

    bool start();

    void stop();

private:

    void receiveRequest(const std::string& request);

    void sendResponse();

    UserInterfaceQueue* userInterfaceQueue_ = nullptr;

    bool running_ = false;

};