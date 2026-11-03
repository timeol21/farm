#include "service/task_service.h"

#include <stdexcept>
#include <unordered_set>

#include "config/config_parser.h"
#include "logger/logger.h"

namespace {
constexpr int kDefaultIntervalMs = 1000;
}

TaskService::TaskService(std::shared_ptr<DeviceService> deviceService)
    : deviceService_(std::move(deviceService)), scheduler_(std::make_unique<Scheduler>(deviceService_)), internalEngine_(std::make_unique<InternalEngine>()) {
    if (!deviceService_) {
        throw std::invalid_argument("DeviceService is null");
    }

    scheduler_->setFinishCallback([this](const TaskControlBlock& task, const nlohmann::json& result) {
        forwardTaskFinished(task, result);
    });
    internalEngine_->setFinishCallback([this](const TaskControlBlock& task, const nlohmann::json& result) {
        forwardTaskFinished(task, result);
    });

    registerDefaultCommands();
    registerDefaultInternalTasks();
    validateConfiguredCommands();
}

TaskService::~TaskService() {
    stop();
}

void TaskService::start() {
    scheduler_->start();
    internalEngine_->start();
    LOG_INFO("[TaskService] Started");
}

void TaskService::stop() {
    internalEngine_->stop();
    scheduler_->stop();
    LOG_INFO("[TaskService] Stopped");
}

void TaskService::submitImmediateTask(const TaskControlBlock& task) {
    scheduler_->submitTask(task);
}

void TaskService::registerCommand(const std::string& cmd, Scheduler::CommandHandler handler) {
    registerExternalCommand(cmd, std::move(handler));
}

void TaskService::setFinishCallback(TaskFinishCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    finishCallback_ = std::move(callback);
}

bool TaskService::hasCommand(const std::string& cmd) const {
    auto it = commandVisibility_.find(cmd);
    return it != commandVisibility_.end() && it->second && scheduler_->hasCommand(cmd);
}

bool TaskService::setDeviceInterval(const std::string& deviceId, int intervalMs) {
    if(intervalMs <= 0 || !deviceService_->hasDevice(deviceId)){
        return false;
    }
    return internalEngine_->setDeviceInterval(deviceId, intervalMs);
}

bool TaskService::resetDeviceInterval(const std::string& deviceId){
    return internalEngine_->resetDeviceInterval(deviceId);
}

void TaskService::registerDefaultCommands() {
    registerExternalCommand ("queryDevice", [this](TaskControlBlock& task) {
        if(task.deviceId.empty()) throw std::runtime_error("DeviceId is missing");
        auto res = deviceService_->getDeviceState(task.deviceId);
        return nlohmann::json(res);
    });

    registerExternalCommand ("queryDeviceList", [this](TaskControlBlock&) {
        auto res = deviceService_->getDeviceGroupState();
        return nlohmann::json(res);
    });

    registerExternalCommand ("operate_valve", [this](TaskControlBlock& task) {
        if(task.deviceId.empty()) throw std::runtime_error("DeviceId is missing");
        if (!task.params.contains("command")) {
            throw std::runtime_error("Valve command is missing");
        }
        printf("[TaskService] Start execute device");
        auto res = deviceService_->executeDeviceCommand(task.deviceId, task.params);
        return nlohmann::json(res);
    });

    registerInternalCommand ("collectDeviceSnapshot", [this](InternalTaskDefinition& task) {
        if (task.deviceId.empty()) throw std::runtime_error("DeviceId is missing");
        if (!deviceService_->triggerUpdateOnce(task.deviceId)) {
            throw std::runtime_error("Collect device snapshot failed: " + task.deviceId);
        }
        auto res = deviceService_->getDeviceState(task.deviceId);
        return nlohmann::json(res);
    });

    registerInternalCommand ("autoReportAllDevices", [this](InternalTaskDefinition& task) {
        auto groupState = deviceService_->getDeviceGroupState();
        return nlohmann::json(groupState);
    });
}

void TaskService::registerExternalCommand(const std::string& cmd, Scheduler::CommandHandler handler) {
    scheduler_->registerCommand(cmd, std::move(handler));
    commandVisibility_[cmd] = true;
}

void TaskService::registerInternalCommand(const std::string& cmd, InternalEngine::CommandHandler handler){
    internalEngine_->registerCommand(cmd, std::move(handler));
    commandVisibility_[cmd] = false;
}

void TaskService::registerDefaultInternalTasks() {
    const AppConfig& cfg = ConfigParser::getInstance().getConfig();
    auto now = std::chrono::steady_clock::now();

    for(const auto& device : cfg.devices) {
        InternalTaskDefinition task;
        task.taskId = "collect_" + device.id;
        task.name = "collect_" + device.id;
        task.cmd = "collectDeviceSnapshot";
        task.deviceId = device.id;
        task.params = nlohmann::json{
            {"deviceId", device.id},
            {"source", "system"},
            {"trigger", "interval"}
        };
        task.intervalMs = device.interval > 0 ? device.interval : kDefaultIntervalMs;
        task.enabled = true;
        task.nextRunAt = now + std::chrono::milliseconds(task.intervalMs);
        task.source = "system";
        internalEngine_->registerTask(task);
    }

    if(cfg.system.auto_report_enabled) {
        InternalTaskDefinition task;
        task.taskId = "system_auto_report";
        task.name = "Auto report all device status";
        task.cmd = "autoReportAllDevices";
        task.deviceId = "system";
        task.intervalMs = cfg.system.auto_report_interval_ms;
        task.enabled = true;
        task.nextRunAt = now + std::chrono::milliseconds(task.intervalMs);
        task.replyTo = cfg.mqtt.status_report_topic;
        internalEngine_->registerTask(task);
    }
}

void TaskService::validateConfiguredCommands() const {
    const AppConfig& cfg = ConfigParser::getInstance().getConfig();
    std::unordered_set<std::string> allowed(cfg.system.allowed_commands.begin(), cfg.system.allowed_commands.end());

    for (const auto& cmd : cfg.system.allowed_commands){
        auto it = commandVisibility_.find(cmd);
        if (it == commandVisibility_.end() || !it->second) {
            LOG_ERROR("[TaskService] Allowed command has no external handler: " + cmd);
        }
    }

    for (const auto& pair : commandVisibility_) {
        if (pair.second && allowed.find(pair.first) == allowed.end()) {
            LOG_WARNING("[TaskService] Registered external command is not in allowed_commands: " + pair.first);
        }
    }
}

void TaskService::forwardTaskFinished(const TaskControlBlock& task, const nlohmann::json& result) const {
    TaskFinishCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = finishCallback_;
    }
    if(callback) {
        callback(task, result);
    }
}