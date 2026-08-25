#include "sensor_service.h"
#include "logger.h"
#include <chrono>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>

// ===================== Modbus RTU 温湿度传感器读取 =====================

// 串口配置（根据实际硬件配置修改）
static const char* SERIAL_PORT = "/dev/ttyS3";      // 串口设备路径
static const int BAUD_RATE = B9600;                 // 波特率

// 串口文件描述符缓存（避免频繁打开关闭）
static int serial_fd = -1;
static std::mutex serial_mutex;

// CRC16计算（Modbus RTU）
static uint16_t calculateCRC16(const uint8_t* data, int length) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// 打开并配置串口
static int openSerialPort() {
    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        Logger::getInstance().log(LogLevel::ERROR, 
            std::string("无法打开串口: ") + SERIAL_PORT);
        return -1;
    }
    
    // 配置串口参数
    struct termios options;
    tcgetattr(fd, &options);
    
    // 设置波特率
    cfsetispeed(&options, BAUD_RATE);
    cfsetospeed(&options, BAUD_RATE);
    
    // 8N1: 8数据位，无校验，1停止位
    options.c_cflag &= ~PARENB;         // 无校验
    options.c_cflag &= ~CSTOPB;         // 1停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;             // 8数据位
    options.c_cflag |= CLOCAL | CREAD;  // 使能接收，忽略modem控制线
    
    // 原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    
    // 读取超时设置
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;  // 1秒超时
    
    tcsetattr(fd, TCSANOW, &options);
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

// 发送Modbus RTU请求并读取响应
static bool modbusReadRegisters(int fd, uint8_t slave_addr, uint16_t start_reg, 
                                 uint16_t reg_count, uint16_t* values) {
    // 构建Modbus RTU请求帧
    // [从机地址][功能码03][寄存器起始地址高][低][寄存器数量高][低][CRC低][CRC高]
    uint8_t request[8];
    request[0] = slave_addr;
    request[1] = 0x03;  // 功能码：读保持寄存器
    request[2] = (start_reg >> 8) & 0xFF;
    request[3] = start_reg & 0xFF;
    request[4] = (reg_count >> 8) & 0xFF;
    request[5] = reg_count & 0xFF;
    
    uint16_t crc = calculateCRC16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // 清空接收缓冲区
    tcflush(fd, TCIFLUSH);
    
    // 发送请求
    if (write(fd, request, 8) != 8) {
        Logger::getInstance().log(LogLevel::ERROR, "Modbus发送请求失败");
        return false;
    }
    
    // 等待响应
    usleep(50000);  // 50ms等待从机响应
    
    // 读取响应
    // 响应格式：[从机地址][功能码][字节数][数据...][CRC低][CRC高]
    int expected_len = 3 + reg_count * 2 + 2;  // 头部3字节 + 数据 + CRC2字节
    uint8_t response[256];
    int total_read = 0;
    int retry = 0;
    
    while (total_read < expected_len && retry < 10) {
        int n = read(fd, response + total_read, expected_len - total_read);
        if (n > 0) {
            total_read += n;
        } else {
            retry++;
            usleep(10000);
        }
    }
    
    if (total_read < expected_len) {
        Logger::getInstance().log(LogLevel::ERROR, 
            std::string("Modbus响应不完整，期望") + std::to_string(expected_len) + 
            "字节，收到" + std::to_string(total_read) + "字节");
        return false;
    }
    
    // 验证CRC
    uint16_t recv_crc = response[total_read - 2] | (response[total_read - 1] << 8);
    uint16_t calc_crc = calculateCRC16(response, total_read - 2);
    if (recv_crc != calc_crc) {
        Logger::getInstance().log(LogLevel::ERROR, "Modbus CRC校验失败");
        return false;
    }
    
    // 解析数据
    for (int i = 0; i < reg_count; i++) {
        values[i] = (response[3 + i * 2] << 8) | response[3 + i * 2 + 1];
    }
    
    return true;
}

// 从指定温湿度传感器读取数据
static bool readTempHumidityFromModbus(uint8_t slave_addr, uint16_t temp_reg,
                                        float& temperature, float& humidity) {
    std::lock_guard<std::mutex> lock(serial_mutex);
    
    int fd = openSerialPort();
    if (fd < 0) {
        return false;
    }
    
    // 读取2个寄存器（温度和湿度）
    uint16_t values[2] = {0};
    bool success = modbusReadRegisters(fd, slave_addr, temp_reg, 2, values);
    close(fd);
    
    if (success) {
        // 根据传感器数据格式转换
        // 常见格式：数值 / 10.0 得到实际值（如255表示25.5°C）
        temperature = static_cast<int16_t>(values[0]) / 10.0f;
        humidity = values[1] / 10.0f;
        
        Logger::getInstance().log(LogLevel::INFO, 
            std::string("读取温湿度成功[从机") + std::to_string(slave_addr) + 
            "]: T=" + std::to_string(temperature) + 
            "°C, H=" + std::to_string(humidity) + "%");
    }
    
    return success;
}

// ===================== SensorService 实现 =====================

SensorService::SensorService() 
    : running_{false} {
    // 初始化默认传感器ID
    infrared_status_.sensor_id = "infrared_01";
    infrared_status_.sensor_type = "infrared";
    
    water_status_.sensor_id = "water_01";
    water_status_.sensor_type = "water_immersion";
    
    smoke_status_.sensor_id = "smoke_01";
    smoke_status_.sensor_type = "smoke";
    
    // 初始化多个温湿度传感器配置（应从配置文件读取，数量不固定）
    // 默认配置1个温湿度传感器
    temp_humidity_configs_ = {
        {"temp_humidity_01", 0x01, 0x0000, 0x0001}
    };
    
    // 初始化温湿度传感器状态列表
    for (const auto& config : temp_humidity_configs_) {
        SensorStatusData status;
        status.sensor_id = config.sensor_id;
        status.sensor_type = "temperature_humidity";
        temp_humidity_status_list_.push_back(status);
    }
    
    Logger::getInstance().log(LogLevel::INFO, "传感器服务已创建");
}

SensorService::~SensorService() {
    stop();
    Logger::getInstance().log(LogLevel::INFO, "传感器服务已销毁");
}

long SensorService::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool SensorService::readGPIOPin(int gpio_pin) {
    std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/value";
    std::ifstream gpio_file(gpio_path);
    
    if (gpio_file.is_open()) {
        int value = 0;
        gpio_file >> value;
        gpio_file.close();
        return (value == 1);
    }
    return false;
}

bool SensorService::initGPIOInput(int gpio_pin) {
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
    return true;
}

bool SensorService::initialize() {
    Logger::getInstance().log(LogLevel::INFO, "正在初始化传感器服务...");
    
    try {
        // 初始化GPIO引脚（红外、水浸、烟感传感器）
        initGPIOInput(infrared_gpio_pin_);
        initGPIOInput(water_immersion_gpio_pin_);
        initGPIOInput(smoke_gpio_pin_);
        
        Logger::getInstance().log(LogLevel::INFO, "传感器服务初始化成功");
        return true;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, 
            std::string("传感器服务初始化失败: ") + e.what());
        return false;
    }
}

void SensorService::start() {
    if (running_) {
        Logger::getInstance().log(LogLevel::WARNING, "传感器服务已在运行中");
        return;
    }
    
    running_ = true;
    monitoring_thread_ = std::thread(&SensorService::monitoringLoop, this);
    Logger::getInstance().log(LogLevel::INFO, "传感器服务已启动");
}

void SensorService::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    Logger::getInstance().log(LogLevel::INFO, "传感器服务已停止");
}

bool SensorService::isRunning() const {
    return running_;
}

void SensorService::setAlarmCallback(
    std::function<void(const std::string& alarm_type, const std::string& reason)> callback) {
    alarm_callback_ = callback;
    Logger::getInstance().log(LogLevel::INFO, "已设置告警回调函数");
}

void SensorService::setAlarmTaskCallback(AlarmTaskCallback callback) {
    alarm_task_callback_ = callback;
    Logger::getInstance().log(LogLevel::INFO, "已设置报警任务回调函数（用于自动提交报警任务）");
}

void SensorService::monitoringLoop() {
    Logger::getInstance().log(LogLevel::INFO, "传感器监控循环已启动");
    
    const int POLL_INTERVAL_MS = 100;  // 100ms轮询间隔
    
    while (running_) {
        try {
            long current_time = getCurrentTimestamp();
            
            // 读取红外传感器
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                bool triggered = readGPIOPin(infrared_gpio_pin_);
                if (triggered != infrared_status_.triggered) {
                    infrared_status_.triggered = triggered;
                    infrared_status_.timestamp = current_time;
                    if (triggered) {
                        checkAndTriggerAlarm(infrared_status_);
                    }
                }
                infrared_status_.is_valid = true;  // GPIO传感器始终在线
            }
            
            // 读取水浸传感器
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                bool triggered = readGPIOPin(water_immersion_gpio_pin_);
                if (triggered != water_status_.triggered) {
                    water_status_.triggered = triggered;
                    water_status_.timestamp = current_time;
                    if (triggered) {
                        checkAndTriggerAlarm(water_status_);
                    }
                }
                water_status_.is_valid = true;  // GPIO传感器始终在线
            }
            
            // 读取烟感传感器
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                bool triggered = readGPIOPin(smoke_gpio_pin_);
                if (triggered != smoke_status_.triggered) {
                    smoke_status_.triggered = triggered;
                    smoke_status_.timestamp = current_time;
                    if (triggered) {
                        checkAndTriggerAlarm(smoke_status_);
                    }
                }
                smoke_status_.is_valid = true;  // GPIO传感器始终在线
            }
            
            // 读取所有温湿度传感器数据（通过Modbus）
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                for (size_t i = 0; i < temp_humidity_configs_.size() && i < temp_humidity_status_list_.size(); ++i) {
                    const auto& config = temp_humidity_configs_[i];
                    auto& status = temp_humidity_status_list_[i];
                    
                    float temp = 0.0f, hum = 0.0f;
                    bool success = readTempHumidityFromModbus(config.slave_addr, config.temp_register, temp, hum);
                    
                    status.is_valid = success;  // 通信成功则在线，否则离线
                    status.timestamp = current_time;
                    
                    if (success) {
                        // 在线时上报数据
                        status.temperature = temp;
                        status.humidity = hum;
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, 
                std::string("传感器监控循环异常: ") + e.what());
        }
    }
    
    Logger::getInstance().log(LogLevel::INFO, "传感器监控循环已停止");
}

void SensorService::checkAndTriggerAlarm(const SensorStatusData& data) {
    std::string alarm_type;
    std::string sensor_type_code;  // 用于报警任务的类型代码
    std::string reason;
    
    if (data.sensor_type == "infrared" && data.triggered) {
        alarm_type = "入侵检测";
        sensor_type_code = "infrared";
        reason = "红外传感器检测到运动: " + data.sensor_id;
    } else if (data.sensor_type == "water_immersion" && data.triggered) {
        alarm_type = "水浸告警";
        sensor_type_code = "water_immersion";
        reason = "水浸传感器检测到漏水: " + data.sensor_id;
    } else if (data.sensor_type == "smoke" && data.triggered) {
        alarm_type = "烟雾告警";
        sensor_type_code = "smoke";
        reason = "烟雾传感器检测到烟雾: " + data.sensor_id;
    }
    
    if (!alarm_type.empty()) {
        Logger::getInstance().log(LogLevel::WARNING, 
            std::string("触发告警: ") + alarm_type + " - " + reason);
        
        // 调用日志回调（如果设置）
        if (alarm_callback_) {
            try {
                alarm_callback_(alarm_type, reason);
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, 
                    std::string("告警回调异常: ") + e.what());
            }
        }
        
        // 自动提交报警任务到调度器（如果设置了回调）
        if (alarm_task_callback_) {
            try {
                // 构建传感器数据JSON
                nlohmann::json sensorData;
                sensorData["sensor_type"] = data.sensor_type;
                sensorData["triggered"] = data.triggered;
                sensorData["timestamp"] = data.timestamp;
                sensorData["is_valid"] = data.is_valid;
                
                // 调用回调，自动提交报警任务
                alarm_task_callback_(sensor_type_code, data.sensor_id, reason, sensorData);
                
                Logger::getInstance().log(LogLevel::INFO, 
                    std::string("已自动提交报警任务: ") + sensor_type_code + " - " + data.sensor_id);
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, 
                    std::string("提交报警任务异常: ") + e.what());
            }
        }
    }
}

AllSensorStatus SensorService::getAllSensorStatus() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    AllSensorStatus status;
    status.collect_timestamp = getCurrentTimestamp();
    
    // 添加GPIO传感器状态
    status.sensors.push_back(infrared_status_);
    status.sensors.push_back(water_status_);
    status.sensors.push_back(smoke_status_);
    
    // 添加所有温湿度传感器状态
    for (const auto& th_status : temp_humidity_status_list_) {
        status.sensors.push_back(th_status);
    }
    
    return status;
}

std::vector<SensorStatusData> SensorService::readAllTemperatureHumidity() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    // 返回所有温湿度传感器状态的副本
    std::vector<SensorStatusData> result;
    long current_time = getCurrentTimestamp();
    for (auto& status : temp_humidity_status_list_) {
        status.timestamp = current_time;
        result.push_back(status);
    }
    return result;
}
