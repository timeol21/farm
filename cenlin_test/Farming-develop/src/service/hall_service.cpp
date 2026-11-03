#include "service/hall_service.h"
#include "mqtt/mqtt_client.h"
#include "utils/json.hpp"
#include "config/config_parser.h"

HallService::HallService()
	: mqttClient(nullptr), deviceService_(std::make_shared<DeviceService>()), taskService_(std::make_unique<TaskService>(deviceService_)) {
	
	const AppConfig& cfg = ConfigParser::getInstance().getConfig();
	for (const auto& cmd : cfg.system.allowed_commands) {
		allowedCommands_.insert(cmd);
	}
	taskService_->setFinishCallback(
		[this](const TaskControlBlock& task, const nlohmann::json& result) {
			if (mqttClient && !task.replyTo.empty()) {
				mqttClient->publish(task.replyTo, result.dump());
			}
		}
	);
	LOG_INFO("[HallService] Initialized");
}

HallService::~HallService() {
	stop();
}

void HallService::start() {
	if (!deviceService_->init()) {
		LOG_ERROR("[HallService] DeviceService initialization failed");
		return;
	}
	deviceService_->start();
	taskService_->start();
}

void HallService::stop(){
	if (taskService_) {
		taskService_->stop();
	}
	if (deviceService_) {
		deviceService_->stop();
	}
}

void HallService::handleRequest(const Request req) {
	if (!mqttClient) return;

	// 1. 校验命令
	if (!isValidCommand(req.cmd)) {
		Result<std::string> res(400, "Invalid command: " + req.cmd, "");
		mqttClient->publish(req.replyTo, nlohmann::json(res).dump());
		return;
	}
	// 2. 解析payload
	nlohmann::json params;
	try {
		params = nlohmann::json::parse(req.payload);
	}
	catch (...) {
		Result<std::string> res(400, "Invalid JSON payload", "");
		mqttClient->publish(req.replyTo, nlohmann::json(res).dump());
		return;
	}

	// 3. 构建任务
	TaskControlBlock task;
	task.cmd = req.cmd;
	task.params = params;
	task.replyTo = req.replyTo;

	if (params.contains("deviceId")) {
		task.deviceId = params["deviceId"].get<std::string>();
	}
	// std::cout<<"[HallService] Created Task: cmd="<<task.cmd<<", deviceId="<<task.deviceId<<", replyTo="<<task.replyTo<<std::endl;
	taskService_->submitImmediateTask(std::move(task));
}

void HallService::setMqttClient(MqttClient* client) {
	mqttClient = client;
}

bool HallService::isValidCommand(const std::string& cmd) {
	return allowedCommands_.find(cmd) != allowedCommands_.end() && taskService_ && taskService_->hasCommand(cmd);
}