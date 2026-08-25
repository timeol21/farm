#include "controller_service.h"
#include "logger.h"
#include <chrono>
#include <fstream>

// ===================== GPIO 辅助函数 =====================

static long getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static bool initGPIOOutput(int gpio_pin) {
    std::ofstream export_file("/sys/class/gpio/export");
    if (export_file.is_open()) {
        export_file << gpio_pin;
        export_file.close();
    }
    
    std::string direction_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/direction";
    std::ofstream direction_file(direction_path);
    if (direction_file.is_open()) {
        direction_file << "out";
        direction_file.close();
        return true;
    }
    return false;
}

static bool setGPIOValue(int gpio_pin, bool value) {
    std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/value";
    std::ofstream gpio_file(gpio_path);
    
    if (gpio_file.is_open()) {
        gpio_file << (value ? "1" : "0");
        gpio_file.close();
        return true;
    }
    return false;
}

// ===================== DoorLockDevice 实现 =====================

DoorLockDevice::DoorLockDevice(const std::string& id, int gpio_pin)
    : id_(id), gpio_pin_(gpio_pin), state_(DeviceState::UNKNOWN), is_initialized_(false) {
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("创建门锁设备: ") + id + " (GPIO: " + std::to_string(gpio_pin) + ")");
}

bool DoorLockDevice::initialize() {
    Logger::getInstance().log(LogLevel::INFO, std::string("初始化门锁设备: ") + id_);
    
    try {
        if (initGPIOOutput(gpio_pin_)) {
            is_initialized_ = true;
            state_ = DeviceState::OFF; // 默认锁定状态
            setGPIOValue(gpio_pin_, false);
            Logger::getInstance().log(LogLevel::INFO, std::string("门锁设备初始化成功: ") + id_);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, 
            std::string("门锁设备初始化异常: ") + e.what());
        return false;
    }
}

bool DoorLockDevice::turnOn() {
    if (!is_initialized_) return false;
    
    Logger::getInstance().log(LogLevel::INFO, std::string("解锁门锁: ") + id_);
    if (setGPIOValue(gpio_pin_, true)) {
        state_ = DeviceState::ON;
        return true;
    }
    return false;
}

bool DoorLockDevice::turnOff() {
    if (!is_initialized_) return false;
    
    Logger::getInstance().log(LogLevel::INFO, std::string("锁定门锁: ") + id_);
    if (setGPIOValue(gpio_pin_, false)) {
        state_ = DeviceState::OFF;
        return true;
    }
    return false;
}

DeviceState DoorLockDevice::getState() const {
    return state_;
}

bool DoorLockDevice::isAvailable() const {
    return is_initialized_;
}

// ===================== ControllerService 实现 =====================

ControllerService::ControllerService() 
    : running_{false} {
    Logger::getInstance().log(LogLevel::INFO, "控制器服务已创建");
}

ControllerService::~ControllerService() {
    stop();
    Logger::getInstance().log(LogLevel::INFO, "控制器服务已销毁");
}

bool ControllerService::initialize() {
    Logger::getInstance().log(LogLevel::INFO, "正在初始化控制器服务...");
    
    try {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        
        for (auto& device : devices_) {
            if (device && !device->isAvailable()) {
                device->initialize();
            }
        }
        
        Logger::getInstance().log(LogLevel::INFO, "控制器服务初始化成功");
        return true;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, 
            std::string("控制器服务初始化失败: ") + e.what());
        return false;
    }
}

void ControllerService::start() {
    if (running_) {
        Logger::getInstance().log(LogLevel::WARNING, "控制器服务已在运行中");
        return;
    }
    
    running_ = true;
    monitoring_thread_ = std::thread(&ControllerService::monitoringLoop, this);
    Logger::getInstance().log(LogLevel::INFO, "控制器服务已启动");
}

void ControllerService::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    Logger::getInstance().log(LogLevel::INFO, "控制器服务已停止");
}

bool ControllerService::isRunning() const {
    return running_;
}

void ControllerService::addDevice(std::unique_ptr<IControllableDevice> device) {
    if (!device) return;
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    for (const auto& d : devices_) {
        if (d->getId() == device->getId()) {
            Logger::getInstance().log(LogLevel::WARNING, 
                std::string("设备已存在: ") + device->getId());
            return;
        }
    }
    
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("添加设备: ") + device->getId() + 
        " (类型: " + deviceTypeToString(device->getType()) + ")");
    
    devices_.push_back(std::move(device));
}

void ControllerService::addDoorLock(const std::string& lock_id, int gpio_pin) {
    auto device = std::make_unique<DoorLockDevice>(lock_id, gpio_pin);
    device->initialize();
    addDevice(std::move(device));
}

IControllableDevice* ControllerService::findDevice(const std::string& device_id) {
    for (auto& device : devices_) {
        if (device->getId() == device_id) {
            return device.get();
        }
    }
    return nullptr;
}

const IControllableDevice* ControllerService::findDevice(const std::string& device_id) const {
    for (const auto& device : devices_) {
        if (device->getId() == device_id) {
            return device.get();
        }
    }
    return nullptr;
}

bool ControllerService::turnOn(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto* device = findDevice(device_id);
    if (!device) {
        Logger::getInstance().log(LogLevel::WARNING, 
            std::string("设备未找到: ") + device_id);
        return false;
    }
    
    if (device->turnOn()) {
        notifyStateChange(device_id, device->getType(), DeviceState::ON);
        return true;
    }
    return false;
}

bool ControllerService::turnOff(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto* device = findDevice(device_id);
    if (!device) {
        Logger::getInstance().log(LogLevel::WARNING, 
            std::string("设备未找到: ") + device_id);
        return false;
    }
    
    if (device->turnOff()) {
        notifyStateChange(device_id, device->getType(), DeviceState::OFF);
        return true;
    }
    return false;
}

DeviceState ControllerService::getDeviceState(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto* device = findDevice(device_id);
    if (!device) {
        return DeviceState::UNKNOWN;
    }
    return device->getState();
}

void ControllerService::setStateCallback(
    std::function<void(const std::string& device_id, DeviceType type, 
                      DeviceState state, long timestamp)> callback) {
    state_callback_ = callback;
    Logger::getInstance().log(LogLevel::INFO, "已设置设备状态回调函数");
}

void ControllerService::notifyStateChange(const std::string& device_id, 
                                          DeviceType type, DeviceState state) {
    if (state_callback_) {
        try {
            long timestamp = getCurrentTimestamp();
            state_callback_(device_id, type, state, timestamp);
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, 
                std::string("状态回调异常: ") + e.what());
        }
    }
}

void ControllerService::monitoringLoop() {
    Logger::getInstance().log(LogLevel::INFO, "控制器监控循环已启动");
    
    const int POLL_INTERVAL_MS = 100;
    
    while (running_) {
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, 
                std::string("控制器监控循环异常: ") + e.what());
        }
    }
    
    Logger::getInstance().log(LogLevel::INFO, "控制器监控循环已停止");
}
