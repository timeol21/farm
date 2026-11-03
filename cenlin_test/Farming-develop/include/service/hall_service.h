#pragma once

#include <string>
#include <memory>
#include <unordered_set>
#include "config/config.h"
#include "service/device_service.h"
#include "service/task_service.h"
#include "utils/json.hpp"

class MqttClient;
class IInferenceEngine;

class HallService {
private:
	MqttClient* mqttClient;
	std::shared_ptr<DeviceService> deviceService_;
	std::unique_ptr<TaskService> taskService_;
	std::unordered_set<std::string> allowedCommands_;

	bool isValidCommand(const std::string& cmd);
public:
	HallService();
	~HallService();

	void setMqttClient(MqttClient* client);
	void handleRequest(const Request req);

	void start();
	void stop();
};