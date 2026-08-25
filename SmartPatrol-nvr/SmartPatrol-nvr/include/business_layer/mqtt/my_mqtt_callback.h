#ifndef MY_MQTT_CALLBACK_H
#define MY_MQTT_CALLBACK_H

#include "mqtt_command_dispatcher.h"
#include "async_client.h"

class MyMqttCallback : public virtual mqtt::callback {
public:
    MyMqttCallback(MqttCommandDispatcher& dispatcher)
        : dispatcher_(dispatcher) {}

    void message_arrived(mqtt::const_message_ptr msg) override 
    {
        dispatcher_.onMessage(msg->get_topic(), msg->to_string());
    }

private:
    MqttCommandDispatcher& dispatcher_;
};

#endif
