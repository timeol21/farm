#include "service/device_service.h"
#include "device/device_factory.h"
#include "config/config_parser.h"
#include <iostream>

DeviceService::DeviceService() {}

DeviceService::~DeviceService() {}

bool DeviceService::init() {
	LOG_INFO("[DeviceService] Starting inirialization...");
	if(!initConnectors()){
		LOG_WARNING("[DeviceService] Some connectors failed to initialize");
	}

	if (!initProtocols()) {
        LOG_WARNING("[DeviceService] Some protocols failed to initialize");
    }

	if (!initDevices()) {
        LOG_WARNING("[DeviceService] Some devices failed to initialize");
    }

	LOG_INFO("[DeviceService] initialization finished");
	return true;
}

bool DeviceService::initConnectors() {
    return true;
}

bool DeviceService::initProtocols() {
    return true;
}

bool DeviceService::initDevices() {
    const AppConfig& cfg = ConfigParser::getInstance().getConfig();
    bool allSuccess = true;

    for (const auto& devCfg : cfg.devices) {
        // 工厂只负责装配
        auto device = DeviceFactory::createDevice(devCfg);
        if (!device) {
            LOG_ERROR("[DeviceService] Create device failed: " + devCfg.id);
            allSuccess = false;
            continue;
        }

        // 初始化并加入设备列表
        if (device->init()) {
            addDevice(std::move(device), devCfg.interval);
            LOG_INFO("[DeviceService] Device ready: " + devCfg.id);
        } else {
            LOG_ERROR("[DeviceService] Device init failed: " + devCfg.id);
            allSuccess = false;
        }
    }

    return allSuccess;
}

void DeviceService::start(){
	if(running_) return;
	running_ = true;

	workerThread_ = std::thread(&DeviceService::pollingWorker, this);
	LOG_INFO("[DeviceService] DeviceService started");
}

void DeviceService::stop() {
	if (!running_) return;

	running_ = false;

	if (workerThread_.joinable()) {
		workerThread_.join();
	}

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& pair : devices_){
		pair.second->stop();
	}
    nextPollingTimes_.clear();
	LOG_INFO("[DeviceService] DeviceService stopped");
}

void DeviceService::pollingWorker() {
	while (running_) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void DeviceService::addDevice(std::unique_ptr<IDevice> dev, int intervalMs){
	std::lock_guard<std::mutex> lock(mutex_);
	if(!dev) return;

	std::string id = dev->getDeviceId();
	if(devices_.find(id) != devices_.end()){
		LOG_WARNING("[DeviceService] Device "+ id + " already exists, overwriting");
	}

	devices_[id] = std::move(dev);
    deviceIntervalsMs_[id] = intervalMs > 0 ? intervalMs : 1000;
    nextPollingTimes_[id] = std::chrono::steady_clock::now();
	LOG_INFO("[DeviceService] Device " + id + " added");
}

Result<DeviceGroupState> DeviceService::getDeviceGroupState() {
	std::lock_guard<std::mutex> lock(mutex_);
	DeviceGroupState groupState = getDeviceGroupStateInternal();
	return Result<DeviceGroupState>(200, "Device group state retrieved successfully", groupState);
}

Result<DeviceState> DeviceService::getDeviceState(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(mutex_);

	if(devices_.find(deviceId) == devices_.end()){
		// std::cout<<"[DeviceService] Device not found: "<<deviceId<<std::endl;
		return Result<DeviceState>(404, "Device not found: " + deviceId, DeviceState());
	}
	// std::cout<<"[DeviceService] Getting state for device: "<<deviceId<<std::endl;
	DeviceState deviceState = getDeviceStateInternal(deviceId);
	return Result<DeviceState>(200, "Device state retrieved successfully for device: " + deviceId, deviceState);
}

bool DeviceService::hasDevice(const std::string& deviceId) const{
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.find(deviceId) != devices_.end();
}

std::vector<std::string> DeviceService::listDeviceIds() const {
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> ids;
	ids.reserve(devices_.size());
	for (const auto& pair : devices_) {
		ids.push_back(pair.first);
	}
	return ids;
}

bool DeviceService::triggerUpdateOnce(const std::string& deviceId) {
	IDevice* device = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = devices_.find(deviceId);
		if (it == devices_.end()) {
			return false;
		}
		device = it->second.get();
	}

	try {
		return device->update();
	}
	catch (const std::exception& e) {
		LOG_ERROR("[DeviceService] Error updating device " + deviceId + ": " + e.what());
		return false;
	}
}

Result<DeviceState> DeviceService::executeDeviceCommand(const std::string& deviceId, const nlohmann::json& params) {
	IActuator* actuator = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = devices_.find(deviceId);
		if (it == devices_.end()) {
			return Result<DeviceState>(404, "Device not found: " + deviceId, DeviceState());
		}
		actuator = dynamic_cast<IActuator*>(it->second.get());
		if (!actuator) {
			return Result<DeviceState>(400, "Device is not actuator: " + deviceId, it->second->getState());
		}
	}

	try {
		if (!actuator->execute(params)) {
			return Result<DeviceState>(500, "Operate device failed: " + deviceId, actuator->getState());
		}
		return Result<DeviceState>(200, "Operate device successfully", actuator->getState());
	}
	catch (const std::exception& e) {
		LOG_ERROR("[DeviceService] Error executing command for device " + deviceId + ": " + e.what());
		return Result<DeviceState>(500, "Operate device exception: " + std::string(e.what()), actuator->getState());
	}
}

int DeviceService::getDefaultIntervalMs(const std::string& deviceId) const {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = deviceIntervalsMs_.find(deviceId);
	if (it == deviceIntervalsMs_.end()) {
		return 0;
	}
	return it->second;
}

DeviceGroupState DeviceService::getDeviceGroupStateInternal() {
	DeviceGroupState groupState;
	groupState.timestamp = std::time(nullptr);
	for (const auto& pair : devices_) {
		DeviceState state = pair.second->getState();
		groupState.devices[pair.first] = state;
		// 在线数量逻辑在此添加
	}
	return groupState;
}

DeviceState DeviceService::getDeviceStateInternal(const std::string& deviceId) {
	auto it = devices_.find(deviceId);
	if(it != devices_.end()){
		// std::cout<<"[DeviceService] Device found: "<<deviceId<<std::endl;
		return it->second->getState();
	}
	return DeviceState();
}