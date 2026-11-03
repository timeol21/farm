#include "service/internal_engine.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "logger/logger.h"

namespace {
constexpr int kDefaultIntervalMs = 1000;
constexpr int kInternalLoopSleepMs = 20;
constexpr int kMaxBackoffLevel = 6;
}

InternalEngine::InternalEngine() = default;

InternalEngine::~InternalEngine() {
    stop();
}

void InternalEngine::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&InternalEngine::loop, this);
    LOG_INFO("[InternalEngine] Started");
}

void InternalEngine::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()){
        thread_.join();
    }
    LOG_INFO("[InternalEngine] Stopped");
}

void InternalEngine::setFinishCallback(FinishCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    finishCallback_ = std::move(callback);
}

void InternalEngine::registerCommand(const std::string& cmd, CommandHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    commandMap_[cmd] = std::move(handler);
}

bool InternalEngine::hasCommand(const std::string& cmd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commandMap_.find(cmd) != commandMap_.end();
}

void InternalEngine::registerTask(const InternalTaskDefinition& task) {
    std::lock_guard<std::mutex> lock(mutex_);

    InternalTaskDefinition copied = task;
    copied.intervalMs = copied.intervalMs > 0 ? copied.intervalMs : kDefaultIntervalMs;
    if (copied.nextRunAt == std::chrono::steady_clock::time_point{}) {
        copied.nextRunAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(copied.intervalMs);
    }

    tasks_[copied.taskId] = copied;
    defaultIntervalsMs_[copied.deviceId] = copied.intervalMs;
    currentIntervalsMs_[copied.deviceId] = copied.intervalMs;
}

bool InternalEngine::setDeviceInterval(const std::string& deviceId, int intervalMs) {
    if (intervalMs <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (currentIntervalsMs_.find(deviceId) == currentIntervalsMs_.end()) {
        return false;
    }

    currentIntervalsMs_[deviceId] = intervalMs;
    for (auto& pair : tasks_) {
        auto& task = pair.second;
        if (task.deviceId == deviceId && task.source == "system") {
            task.intervalMs = intervalMs;
            task.nextRunAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
            task.consecutiveFailures = 0;
            task.backoffLevel = 0;
            task.lastError.clear();
        }
    }
    return true;
}

bool InternalEngine::resetDeviceInterval(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto defaultIt = defaultIntervalsMs_.find(deviceId);
    if (defaultIt == defaultIntervalsMs_.end()) {
        return false;
    }

    currentIntervalsMs_[deviceId] = defaultIt->second;
    for (auto& pair : tasks_) {
        auto& task = pair.second;
        if (task.deviceId == deviceId && task.source == "system") {
            task.intervalMs = defaultIt->second;
            task.nextRunAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(task.intervalMs);
            task.consecutiveFailures = 0;
            task.backoffLevel = 0;
            task.lastError.clear();
        }
    }
    return true;
}

void InternalEngine::loop() {
    using Clock = std::chrono::steady_clock;

    while (running_) {
        std::vector<std::string> dueTaskIds;
        auto now = Clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pair : tasks_) {
                const auto& task = pair.second;
                if (!task.enabled || task.intervalMs <= 0) {
                    continue;
                }
                if (now >= task.nextRunAt) {
                    dueTaskIds.push_back(pair.first);
                }
            }
        }

        for (const auto& taskId : dueTaskIds) {
            InternalTaskDefinition task;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = tasks_.find(taskId);
                if (it == tasks_.end() || !it->second.enabled) {
                    continue;
                }
                task = it->second;
            }

            TaskControlBlock callbackTask = buildCallbackTask(task);
            callbackTask.state = TaskState::RUNNING;
            callbackTask.startTime = Clock::now();

            try {
                nlohmann::json result = executeTask(task);
                auto finishedAt = Clock::now();
                callbackTask.state = TaskState::FINISHED;
                callbackTask.duration = finishedAt - callbackTask.startTime;
                handleTaskSuccess(taskId, finishedAt);
                notifyTaskFinished(callbackTask, result);
            }
            catch (const std::exception& e) {
                auto failedAt = Clock::now();
                callbackTask.state = TaskState::FAILED;
                callbackTask.duration = failedAt - callbackTask.startTime;
                handleTaskFailure(taskId, failedAt, e.what());
                notifyTaskFinished(callbackTask, nlohmann::json{
                    {"code", 500},
                    {"msg", e.what()},
                    {"data", nullptr}
                });
                LOG_ERROR(std::string("[InternalEngine] Internal task failed: ") + taskId + ": " + e.what());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kInternalLoopSleepMs));
    }
}

nlohmann::json InternalEngine::executeTask(InternalTaskDefinition& task) const {
    CommandHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = commandMap_.find(task.cmd);
        if (it == commandMap_.end()) {
            throw std::runtime_error("Unknown internal command: " + task.cmd);
        }
        handler = it->second;
    }
    return handler(task);
}

TaskControlBlock InternalEngine::buildCallbackTask(const InternalTaskDefinition& task) {
    TaskControlBlock callbackTask;
    callbackTask.id = nextTaskId_++;
    callbackTask.state = TaskState::READY;
    callbackTask.enqueueTime = std::chrono::steady_clock::now();
    callbackTask.name = task.name;
    callbackTask.deviceId = task.deviceId;
    callbackTask.cmd = task.cmd;
    callbackTask.params = task.params;
    callbackTask.replyTo = task.replyTo;
    return callbackTask;
}

void InternalEngine::notifyTaskFinished(const TaskControlBlock& task, const nlohmann::json& result) const {
    FinishCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = finishCallback_;
    }
    if (callback) {
        callback(task, result);
    }
}

void InternalEngine::handleTaskSuccess(const std::string& taskId, const std::chrono::steady_clock::time_point& now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return;
    }

    auto& task = it->second;
    task.consecutiveFailures = 0;
    task.backoffLevel = 0;
    task.lastError.clear();
    task.lastSuccessAt = now;

    int intervalMs = task.intervalMs > 0 ? task.intervalMs : kDefaultIntervalMs;
    auto currentIt = currentIntervalsMs_.find(task.deviceId);
    if (currentIt != currentIntervalsMs_.end() && currentIt->second > 0) {
        intervalMs = currentIt->second;
    }
    task.intervalMs = intervalMs;
    task.nextRunAt = now + std::chrono::milliseconds(intervalMs);
}

void InternalEngine::handleTaskFailure(const std::string& taskId, const std::chrono::steady_clock::time_point& now, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return;
    }

    auto& task = it->second;
    task.consecutiveFailures += 1;
    task.backoffLevel = std::min(task.backoffLevel + 1, kMaxBackoffLevel);
    task.lastFailureAt = now;
    task.lastError = error;

    int baseIntervalMs = task.intervalMs > 0 ? task.intervalMs : kDefaultIntervalMs;
    auto currentIt = currentIntervalsMs_.find(task.deviceId);
    if (currentIt != currentIntervalsMs_.end() && currentIt->second > 0) {
        baseIntervalMs = currentIt->second;
    }
    int delayMs = baseIntervalMs * (1 << task.backoffLevel);
    task.nextRunAt = now + std::chrono::milliseconds(delayMs);
}