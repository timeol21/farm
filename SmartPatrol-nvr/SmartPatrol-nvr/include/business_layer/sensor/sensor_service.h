#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include "json.hpp"

// 传感器数据结构（用于状态上报）
struct SensorStatusData {
    std::string sensor_id;
    std::string sensor_type;    // "infrared", "water_immersion", "smoke", "temperature_humidity"
    bool is_valid = false;     // 在线/离线状态
    bool triggered = false;     // GPIO传感器触发状态
    float temperature = 0.0f;   // 温湿度传感器温度
    float humidity = 0.0f;      // 温湿度传感器湿度
    long timestamp = 0;
};

// 所有传感器状态汇总（仅包含传感器，门锁在控制器中管理）
struct AllSensorStatus {
    std::vector<SensorStatusData> sensors;
    long collect_timestamp = 0;
};

// 温湿度传感器配置
struct TempHumiditySensorConfig {
    std::string sensor_id;      // 传感器ID
    uint8_t slave_addr;         // Modbus从机地址
    uint16_t temp_register;     // 温度寄存器地址
    uint16_t humidity_register; // 湿度寄存器地址
};

// 报警回调类型定义（用于自动提交报警任务）
// 参数: alarmType, sensorId, reason, sensorData
using AlarmTaskCallback = std::function<void(const std::string&, const std::string&, 
                                              const std::string&, const nlohmann::json&)>;

// 传感器服务接口
// 红外、水浸、烟感：后台监听，触发告警
// 温湿度：主动读取数据
class ISensorService {
public:
    virtual ~ISensorService() = default;
    virtual bool initialize() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    
    // 读取所有传感器状态（用于定时上报）
    virtual AllSensorStatus getAllSensorStatus() = 0;
    
    // 读取所有温湿度传感器（主动读取）
    virtual std::vector<SensorStatusData> readAllTemperatureHumidity() = 0;
    
    // 设置报警任务回调（当传感器异常时自动提交报警任务）
    virtual void setAlarmTaskCallback(AlarmTaskCallback callback) = 0;
    
    // 告警回调（红外、水浸、烟感触发时调用）- 保留用于日志等
    virtual void setAlarmCallback(std::function<void(const std::string& alarm_type, 
                                                   const std::string& reason)> callback) = 0;
};

// 传感器服务实现类
// 红外、水浸、烟感：后台监听，触发告警
// 温湿度：主动读取数据
class SensorService : public ISensorService {
public:
    SensorService();
    ~SensorService();
    
    bool initialize() override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    
    // 读取所有传感器状态（用于定时上报）
    AllSensorStatus getAllSensorStatus() override;
    
    // 读取所有温湿度传感器（主动读取）
    std::vector<SensorStatusData> readAllTemperatureHumidity() override;
    
    // 设置报警任务回调（当传感器异常时自动提交报警任务到调度器）
    void setAlarmTaskCallback(AlarmTaskCallback callback) override;
    
    // 告警回调（保留用于日志等）
    void setAlarmCallback(std::function<void(const std::string& alarm_type, 
                                           const std::string& reason)> callback) override;
    
private:
    void monitoringLoop();
    void checkAndTriggerAlarm(const SensorStatusData& data);
    long getCurrentTimestamp();
    
    // GPIO操作（模拟/实际）
    bool readGPIOPin(int gpio_pin);
    bool initGPIOInput(int gpio_pin);
    
    std::thread monitoring_thread_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&, const std::string&)> alarm_callback_;
    AlarmTaskCallback alarm_task_callback_;  // 用于自动提交报警任务
    
    mutable std::mutex status_mutex_;
    
    // 固定配置的GPIO引脚（从配置文件读取）
    int infrared_gpio_pin_ = 17;
    int water_immersion_gpio_pin_ = 18;
    int smoke_gpio_pin_ = 27;
    
    // 温湿度传感器配置列表
    std::vector<TempHumiditySensorConfig> temp_humidity_configs_;
    
    // 当前传感器状态缓存
    SensorStatusData infrared_status_;
    SensorStatusData water_status_;
    SensorStatusData smoke_status_;
    std::vector<SensorStatusData> temp_humidity_status_list_;  // 多个温湿度传感器状态
};

#endif