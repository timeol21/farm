#pragma once

#include "logger/logger.h"
#include "config/config.h"
#include "device/i_device.h"
#include "actuator/i_actuator.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

class DeviceService {
public:
	DeviceService();
	~DeviceService();

	Result<DeviceGroupState> getDeviceGroupState();
	Result<DeviceState> getDeviceState(const std::string& deviceId);
	Result<DeviceState> executeDeviceCommand(const std::string& deviceId, const nlohmann::json& params);
	bool hasDevice(const std::string& deviceId) const;
	std::vector<std::string> listDeviceIds() const;
	bool triggerUpdateOnce(const std::string& deviceId);
	
	int getDefaultIntervalMs(const std::string& deviceId) const;

	void addDevice(std::unique_ptr<IDevice> dev, int intervalMs);

	bool init();
	void start();
	void stop();
private:
	DeviceGroupState getDeviceGroupStateInternal();
	DeviceState getDeviceStateInternal(const std::string& deviceId);
	void pollingWorker();

	bool initConnectors();
	bool initProtocols();
	bool initDevices();
private:
	std::atomic<bool> running_{ false };
	std::thread workerThread_;
	std::unordered_map<std::string, std::unique_ptr<IDevice>> devices_;
	std::unordered_map<std::string, int> deviceIntervalsMs_;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> nextPollingTimes_;
	mutable std::mutex mutex_;
};