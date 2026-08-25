#include "userinterface_layer/user_interface.h"
#include "common/layer_buffer_queue/layer_buffer_queue.h"

#include "userinterface_layer/bufferqueue/user_interface_buffer_queue.h"
#include "userinterface_layer/http/http_server.h"
#include "userinterface_layer/mqtt/mqtt_client.h"
#include "userinterface_layer/parser/mqtt_parser.h"
#include "userinterface_layer/tcp/tcp_server.h"
#include "userinterface_layer/rtsp/rtsp_client.h"


UserInterface& UserInterface::instance()
{
    static UserInterface instance;

    return instance;
}


bool UserInterface::initialize()
{

    userInterfaceQueue_ = &UserInterfaceQueue::instance();

    httpServer_ = std::make_unique<HttpServer>();

    mqttClient_ = std::make_unique<MqttClient>();

    mqttParser_ = std::make_unique<MqttParser>(*userInterfaceQueue_,LayerBufferQueue::instance());

    tcpServer_ = std::make_unique<TcpServer>();

    rtspClient_ = std::make_unique<RtspClient>();

    httpServer_->initialize(userInterfaceQueue_);

    mqttClient_->initialize(userInterfaceQueue_);

    tcpServer_->initialize(userInterfaceQueue_);

    rtspClient_->initialize();

    return true;
}


bool UserInterface::start()
{

    mqttParser_->start();

    mqttClient_->start();

    httpServer_->start();

    tcpServer_->start();

    rtspClient_->start();

    return true;
}


void UserInterface::stop()
{

    if(httpServer_)
    {
        httpServer_->stop();
    }

    if(mqttParser_)
    {
        mqttParser_->stop();
    }


    if(mqttClient_)
    {
        mqttClient_->stop();
    }


    if(tcpServer_)
    {
        tcpServer_->stop();
    }


    if(rtspClient_)
    {
        rtspClient_->stop();
    }

}



UserInterfaceQueue& UserInterface::userInterfaceQueue()
{
    return *userInterfaceQueue_;
}