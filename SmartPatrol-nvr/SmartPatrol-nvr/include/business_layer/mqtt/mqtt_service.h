#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <mqtt/async_client.h>
#include "job_scheduler.h"
#include "mqtt_command_dispatcher.h"

class MqttService : public virtual mqtt::callback
{
public:
    MqttService(const std::string& serverURI,
                const std::string& clientId,
                ICommandDispatcher* dispatcher);

    void start();

    void connection_lost(const std::string& cause) override; //断连时触发
    void message_arrived(mqtt::const_message_ptr msg) override; //收到消息时触发
    void delivery_complete(mqtt::delivery_token_ptr tok) override {} //消息发布完成时触发
    void publish(const std::string& topic, const std::string& payload, int qos = 1, bool retained = false);
private:
    mqtt::async_client client_;
    ICommandDispatcher* dispatcher_;
};
#endif