#ifndef MQTT_COMMAND_DISPATCHER_H
#define MQTT_COMMAND_DISPATCHER_H

#include <string>
#include <memory>
#include <json.hpp>
#include "job_scheduler.h"
#include "task.h"
#include "icommand_dispatcher.h"
class MqttCommandDispatcher : public ICommandDispatcher
{
public:
    MqttCommandDispatcher(JobScheduler& scheduler);
    void onMessage(const std::string& topic, const std::string& payload) override;

private:
    JobScheduler& scheduler_;
    std::string boxId_;
    void handleGetRealImage(const nlohmann::json& j);
    void handleOperatePlc(const nlohmann::json& j);
    void handleGetPLCDeviceStatus(const nlohmann::json& j);
    void handleUpdateConfig(const nlohmann::json& j);
    void handleGetSensorData(const nlohmann::json& j);
    void handleOperateCar(const nlohmann::json& j);
    void handleGetAllDeviceStatus(const nlohmann::json& j);
    void handleOperatePlcWithVerify(const nlohmann::json& j);
    void handleConfigUpdate(const nlohmann::json& j);
    void handVideoHistory(const nlohmann::json& j);
    void handVideoHistoryFile(const nlohmann::json& j);
};


#endif