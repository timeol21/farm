#include "service/scheduler.h"
#include <stdexcept>
#include <chrono>
#include <iostream>
#include "logger/logger.h"

int Scheduler::nextTaskId_ = 1;

Scheduler::Scheduler(std::shared_ptr<DeviceService> deviceService)
	: deviceService_(deviceService) {
	if (!deviceService_) {
		throw std::invalid_argument("DeviceService is null");
	}
}

Scheduler::~Scheduler() {
	stop();
}

void Scheduler::start() {
	if(thread_.joinable()) return;
	isRunning_ = true;
	thread_ = std::thread(&Scheduler::scheduleLoop, this);
	LOG_INFO("[Scheduler] Started");
}

void Scheduler::stop() {
	isRunning_ = false;
	cv_.notify_all();

	if (thread_.joinable()) {
		thread_.join();
	}
}

void Scheduler::setFinishCallback(TaskFinishCallback callback) {
	std::lock_guard<std::mutex> lock(mutex_);
	finishCallback_ = std::move(callback);
}

void Scheduler::registerCommand(const std::string& cmd, CommandHandler handler) {
	commandMap_[cmd] = std::move(handler);
}

bool Scheduler::hasCommand(const std::string& cmd) const {
	return commandMap_.find(cmd) != commandMap_.end();
}

void Scheduler::submitTask(TaskControlBlock task) {
	{
		std::lock_guard<std::mutex> lock(mutex_);

		task.id = nextTaskId_++;
		task.state = TaskState::READY;
		task.enqueueTime = std::chrono::steady_clock::now();

		readyQueue_.push(std::move(task));
	}
	cv_.notify_one();
}

void Scheduler::scheduleLoop() {
	while (isRunning_) {
		TaskControlBlock task;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [&]() {
				return !readyQueue_.empty() || !isRunning_;
			});
			if (!isRunning_) break;
			task = std::move(readyQueue_.front());
			readyQueue_.pop();
			task.state = TaskState::RUNNING;
			task.startTime = std::chrono::steady_clock::now();
		}
		
		nlohmann::json result;
		try {
			result = executeTask(task);
			task.state = TaskState::FINISHED;
		}
		catch (const std::exception& e) {
			task.state = TaskState::FAILED;
			result = {
				{"code", 500},
				{"msg", e.what()},
				{"data", nullptr}
			};
			LOG_ERROR(std::string("[Scheduler] Task failed: ") + e.what());
		}
		if (finishCallback_) {
			finishCallback_(task, result);
		}
	}
}

nlohmann::json Scheduler::executeTask(TaskControlBlock& task) {
	if(!deviceService_){
		throw std::runtime_error("DeviceService is not initialized");
	}

	auto it = commandMap_.find(task.cmd);
	if(it == commandMap_.end()){
		throw std::runtime_error("Unknown command: " + task.cmd);
	}
	return it->second(task);
}