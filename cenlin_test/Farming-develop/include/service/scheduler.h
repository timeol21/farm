#pragma once

#include <queue>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include "config/config.h"
#include "service/device_service.h"
#include "utils/json.hpp"

using TaskFinishCallback = std::function<void(const TaskControlBlock& task, const nlohmann::json& result)>;

class Scheduler {
public:
	explicit Scheduler(std::shared_ptr<DeviceService> deviceService);
	~Scheduler();

	void start();
	void stop();

	void submitTask(TaskControlBlock task);
	void setFinishCallback(TaskFinishCallback callback);

	using CommandHandler = std::function<nlohmann::json(TaskControlBlock&)>;
	void registerCommand(const std::string& cmd, CommandHandler handler);
	bool hasCommand(const std::string& cmd) const;
private:
	void scheduleLoop();
	nlohmann::json executeTask(TaskControlBlock& task);

private:
	std::shared_ptr<DeviceService> deviceService_;
	TaskFinishCallback finishCallback_;

	std::queue<TaskControlBlock> readyQueue_;

	std::mutex mutex_;
	std::condition_variable cv_;
	std::thread thread_;
	std::atomic<bool> isRunning_{true};
	std::unordered_map<std::string, CommandHandler> commandMap_;

	static int nextTaskId_;
};