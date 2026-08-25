#include "sensor_reader.h"
#include "logger.h"
#include <chrono>
#include <fstream>
#include <sstream>

// ===================== GPIO 辅助函数 =====================

// 模拟读取 GPIO 引脚状态（实际硬件需替换为真实 GPIO 读取）
static bool readGPIOPin(int gpio_pin) {
    // Linux 下的 GPIO 读取示例（通过 sysfs）
    // 实际使用时需要先 export GPIO 并设置为输入模式
    std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/value";
    std::ifstream gpio_file(gpio_path);
    
    if (gpio_file.is_open()) {
        int value = 0;
        gpio_file >> value;
        gpio_file.close();
        return (value == 1);
    }
    
    // 如果无法读取 GPIO，返回 false（未触发）
    // 在开发/测试环境中可使用模拟值
    return false;
}

// 初始化 GPIO 引脚为输入模式
static bool initGPIOPin(int gpio_pin) {
    // Export GPIO
    std::ofstream export_file("/sys/class/gpio/export");
    if (export_file.is_open()) {
        export_file << gpio_pin;
        export_file.close();
    }
    
    // 设置为输入模式
    std::string direction_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/direction";
    std::ofstream direction_file(direction_path);
    if (direction_file.is_open()) {
        direction_file << "in";
        direction_file.close();
        return true;
    }
    
    // 即使 sysfs 操作失败，也返回 true 以便在模拟环境中继续
    return true;
}

// 获取当前时间戳（毫秒）
static long getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ===================== InfraredSensor 实现 =====================

InfraredSensor::InfraredSensor(const std::string& id, int gpio_pin)
    : id_(id), gpio_pin_(gpio_pin), is_initialized_(false) {
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("创建红外传感器: ") + id + " (GPIO引脚: " + std::to_string(gpio_pin) + ")");
}

bool InfraredSensor::initialize() {
    Logger::getInstance().log(LogLevel::INFO, std::string("初始化红外传感器: ") + id_);
    
    try {
        // 初始化 GPIO 引脚
        if (initGPIOPin(gpio_pin_)) {
            is_initialized_ = true;
            Logger::getInstance().log(LogLevel::INFO, std::string("红外传感器初始化成功: ") + id_);
            return true;
        } else {
            Logger::getInstance().log(LogLevel::ERROR, std::string("红外传感器GPIO初始化失败: ") + id_);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("红外传感器初始化异常: ") + e.what());
        return false;
    }
}

std::unique_ptr<SensorData> InfraredSensor::readData() {
    if (!is_initialized_) {
        Logger::getInstance().log(LogLevel::WARNING, std::string("红外传感器未初始化: ") + id_);
        return nullptr;
    }
    
    try {
        bool triggered = readGPIOPin(gpio_pin_);
        long timestamp = getCurrentTimestamp();
        return std::make_unique<SensorData>(id_, getType(), triggered, timestamp);
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("读取红外传感器数据失败: ") + e.what());
        return nullptr;
    }
}

bool InfraredSensor::isAvailable() const {
    return is_initialized_;
}

// ===================== WaterImmersionSensor 实现 =====================

WaterImmersionSensor::WaterImmersionSensor(const std::string& id, int gpio_pin)
    : id_(id), gpio_pin_(gpio_pin), is_initialized_(false) {
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("创建水浸传感器: ") + id + " (GPIO引脚: " + std::to_string(gpio_pin) + ")");
}

bool WaterImmersionSensor::initialize() {
    Logger::getInstance().log(LogLevel::INFO, std::string("初始化水浸传感器: ") + id_);
    
    try {
        if (initGPIOPin(gpio_pin_)) {
            is_initialized_ = true;
            Logger::getInstance().log(LogLevel::INFO, std::string("水浸传感器初始化成功: ") + id_);
            return true;
        } else {
            Logger::getInstance().log(LogLevel::ERROR, std::string("水浸传感器GPIO初始化失败: ") + id_);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("水浸传感器初始化异常: ") + e.what());
        return false;
    }
}

std::unique_ptr<SensorData> WaterImmersionSensor::readData() {
    if (!is_initialized_) {
        Logger::getInstance().log(LogLevel::WARNING, std::string("水浸传感器未初始化: ") + id_);
        return nullptr;
    }
    
    try {
        bool triggered = readGPIOPin(gpio_pin_);
        long timestamp = getCurrentTimestamp();
        return std::make_unique<SensorData>(id_, getType(), triggered, timestamp);
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("读取水浸传感器数据失败: ") + e.what());
        return nullptr;
    }
}

bool WaterImmersionSensor::isAvailable() const {
    return is_initialized_;
}

// ===================== SmokeSensor 实现 =====================

SmokeSensor::SmokeSensor(const std::string& id, int gpio_pin)
    : id_(id), gpio_pin_(gpio_pin), is_initialized_(false) {
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("创建烟雾传感器: ") + id + " (GPIO引脚: " + std::to_string(gpio_pin) + ")");
}

bool SmokeSensor::initialize() {
    Logger::getInstance().log(LogLevel::INFO, std::string("初始化烟雾传感器: ") + id_);
    
    try {
        if (initGPIOPin(gpio_pin_)) {
            is_initialized_ = true;
            Logger::getInstance().log(LogLevel::INFO, std::string("烟雾传感器初始化成功: ") + id_);
            return true;
        } else {
            Logger::getInstance().log(LogLevel::ERROR, std::string("烟雾传感器GPIO初始化失败: ") + id_);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("烟雾传感器初始化异常: ") + e.what());
        return false;
    }
}

std::unique_ptr<SensorData> SmokeSensor::readData() {
    if (!is_initialized_) {
        Logger::getInstance().log(LogLevel::WARNING, std::string("烟雾传感器未初始化: ") + id_);
        return nullptr;
    }
    
    try {
        bool triggered = readGPIOPin(gpio_pin_);
        long timestamp = getCurrentTimestamp();
        return std::make_unique<SensorData>(id_, getType(), triggered, timestamp);
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, std::string("读取烟雾传感器数据失败: ") + e.what());
        return nullptr;
    }
}

bool SmokeSensor::isAvailable() const {
    return is_initialized_;
}

// ===================== SensorPollingReader 实现 =====================

SensorPollingReader::SensorPollingReader() 
    : running_(false), polling_interval_(1000) {
    Logger::getInstance().log(LogLevel::INFO, "传感器轮询读取器已创建");
}

SensorPollingReader::~SensorPollingReader() {
    stopPolling();
    Logger::getInstance().log(LogLevel::INFO, "传感器轮询读取器已销毁");
}

void SensorPollingReader::addSensor(std::unique_ptr<ISensor> sensor) {
    if (sensor) {
        Logger::getInstance().log(LogLevel::INFO, 
            std::string("添加传感器: ") + sensor->getId() + " (类型: " + sensor->getType() + ")");
        sensors_.push_back(std::move(sensor));
    }
}

void SensorPollingReader::setListener(std::shared_ptr<ISensorListener> listener) {
    external_listener_ = listener;
    Logger::getInstance().log(LogLevel::INFO, "已设置外部传感器监听器");
}

void SensorPollingReader::startPolling(int interval_ms) {
    if (running_) {
        Logger::getInstance().log(LogLevel::WARNING, "传感器轮询已在运行中");
        return;
    }
    
    polling_interval_ = interval_ms;
    running_ = true;
    polling_thread_ = std::thread(&SensorPollingReader::pollingLoop, this);
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("传感器轮询已启动，间隔: ") + std::to_string(interval_ms) + " ms");
}

void SensorPollingReader::stopPolling() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
    
    Logger::getInstance().log(LogLevel::INFO, "传感器轮询已停止");
}

void SensorPollingReader::onSensorData(const SensorData& data) {
    Logger::getInstance().log(LogLevel::INFO, 
        std::string("收到传感器数据: ID=") + data.sensor_id + 
        " 类型=" + data.sensor_type + 
        " 状态=" + (data.state ? "触发" : "未触发"));
    
    // 转发给外部监听器
    if (external_listener_) {
        try {
            external_listener_->onSensorData(data);
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, 
                std::string("调用外部监听器失败: ") + e.what());
        }
    }
}

void SensorPollingReader::pollingLoop() {
    Logger::getInstance().log(LogLevel::INFO, "传感器轮询循环已启动");
    
    while (running_) {
        try {
            // 轮询所有传感器
            for (auto& sensor : sensors_) {
                if (sensor && sensor->isAvailable()) {
                    auto data = sensor->readData();
                    if (data) {
                        onSensorData(*data);
                    }
                }
            }
            
            // 等待轮询间隔
            std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval_));
            
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, 
                std::string("传感器轮询循环异常: ") + e.what());
        }
    }
    
    Logger::getInstance().log(LogLevel::INFO, "传感器轮询循环已停止");
}
