#include "userinterface_layer/tcp/tcp_server.h"

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"

bool TcpServer::initialize(UserInterfaceQueue* queue)
{
    userInterfaceQueue_ = queue;

    return true;

}

bool TcpServer::start()
{
    running_ = true;

    return true;

}


void TcpServer::stop()
{
    running_ = false;

}

// void TcpServer::receiveData(const std::string& data)
// {

//     UserMessage message;
//     message.data = data;

//     UserInterfaceQueue_->pushTcpReceive(message);

// }