#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "config/config.h"

class InternalEngine {
public:
    using CommandHandler = std::function<nlohmann::json(InternalTaskDefinition&)>;
    using FinishCallback = std::function<void(const TaskControlBlock&, const nlohmann::json&)>;

    InternalEngine();
    ~InternalEngine();

    void start();
    void stop();

    void setFinishCallback(FinishCallback callback);
    void registerCommand(const std::string& cmd, CommandHandler handler);
    bool hasCommand(const std::string& cmd) const;
    void registerTask(const InternalTaskDefinition& task);

    bool setDeviceInterval(const std::string& deviceId, int intervalMs);
    bool resetDeviceInterval(const std::string& deviceId);
private:
    void loop();
    nlohmann::json executeTask(InternalTaskDefinition& task) const;
    TaskControlBlock buildCallbackTask(const InternalTaskDefinition& task);
    void notifyTaskFinished(const TaskControlBlock& task, const nlohmann::json& result) const;
    void handleTaskSuccess(const std::string& taskId, const std::chrono::steady_clock::time_point& now);
    void handleTaskFailure(const std::string& taskId, const std::chrono::steady_clock::time_point& now, const std::string& error);
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CommandHandler> commandMap_;
    std::unordered_map<std::string, InternalTaskDefinition> tasks_;
    std::unordered_map<std::string, int> defaultIntervalsMs_;
    std::unordered_map<std::string, int> currentIntervalsMs_;
    FinishCallback finishCallback_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    mutable std::atomic<int> nextTaskId_{ 1 };
};