#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "scheduler.h"
#include "internal_engine.h"
#include "config/config.h"

class TaskService {
public:
    explicit TaskService(std::shared_ptr<DeviceService> deviceService);
    ~TaskService();

    void start();
    void stop();

    void submitImmediateTask(const TaskControlBlock& task);
    void registerCommand(const std::string& cmd, Scheduler::CommandHandler handler);
    void setFinishCallback(TaskFinishCallback callback);
    bool hasCommand(const std::string& cmd) const;

    bool setDeviceInterval(const std::string& deviceId, int intervalMs);
    bool resetDeviceInterval(const std::string& deviceId);
private:
    void registerDefaultCommands();
    void registerExternalCommand(const std::string& cmd, Scheduler::CommandHandler handler);
    void registerInternalCommand(const std::string& cmd, InternalEngine::CommandHandler handler);
    void registerDefaultInternalTasks();
    void validateConfiguredCommands() const;
    void forwardTaskFinished(const TaskControlBlock& task, const nlohmann::json& result) const;
private:
    std::shared_ptr<DeviceService> deviceService_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<InternalEngine> internalEngine_;
    mutable std::mutex mutex_;
    TaskFinishCallback finishCallback_;
    std::unordered_map<std::string, bool> commandVisibility_;
};