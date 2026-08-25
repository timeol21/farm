#include "userinterface_layer/http/http_server.h"

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

bool HttpServer::initialize(UserInterfaceQueue* queue)
{

    userInterfaceQueue_ = queue;
    return true;

}


bool HttpServer::start()
{
    running_ = true;

    return true;

}

void HttpServer::stop()
{
    running_ = false;

}

void HttpServer::receiveRequest(const std::string& request)
{

    UserMessage message;

    message.data = request;

    userInterfaceQueue_->pushHttpReceive(message);

}