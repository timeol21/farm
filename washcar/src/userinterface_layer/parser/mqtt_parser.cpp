#include "userinterface_layer/parser/mqtt_parser.h"

#include <nlohmann/json.hpp>

#include <chrono>

using json = nlohmann::json;

MqttParser::MqttParser(UserInterfaceQueue& inputQueue,LayerBufferQueue& outputQueue)
:
inputQueue_(inputQueue),
outputQueue_(outputQueue),
running_(false)
{

}

bool MqttParser::start()
{

    if(running_)
    {
        return true;
    }

    running_ = true;

    thread_ = std::thread(&MqttParser::run,this);

    return true;

}

void MqttParser::run()
{

    while(running_)
    {

        process();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    }

}

bool MqttParser::process()
{

    UserMessage message;

    if(!inputQueue_.popMqttReceive(message))
    {
        return false;
    }

    CommandMessage command;

    if(!parseMessage(message,command))
    {
        return false;
    }

    return outputQueue_.push(command);

}

bool MqttParser::parseMessage(const UserMessage& input,CommandMessage& output)
{

    try
    {

        json data =json::parse(input.data);

        /*
            MQTT JSON格式:

            {
                "type":"START_WASH",
                "deviceId":"box001",
                "parameter":"",
                "source":"mqtt"
            }

        */

        std::string type =data.value("type","");

        if(type == "START_WASH")
        {
            output.type = CommandType::START_WASH;
        }
        else if(type == "STOP_WASH")
        {
            output.type = CommandType::STOP_WASH;
        }
        else if(type == "PAUSE_WASH")
        {
            output.type = CommandType::PAUSE_WASH;
        }
        else if(type == "RESET_DEVICE")
        {
            output.type = CommandType::RESET_DEVICE;
        }
        else if(type == "QUERY_STATUS")
        {
            output.type = CommandType::QUERY_STATUS;
        }
        else
        {
            output.type = CommandType::NONE;
            return false;
        }

        output.deviceId = data.value("deviceId","");

        output.parameter = data.value("parameter","");

        output.source = "mqtt";

        return true;

    }
    catch(...)
    {
        return false;

    }

}

void MqttParser::stop()
{

    running_ = false;

    if(thread_.joinable())
    {
        thread_.join();
    }

}

MqttParser::~MqttParser()
{
    stop();
}