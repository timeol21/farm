#ifndef SENSOR_MONITOR_H
#define SENSOR_MONITOR_H

#include "data_layer/sensor_reader.h"
#include "business_layer/sensor_service.h"
#include "business_layer/video_service.h"
#include "business_layer/interaction_service.h"
#include "business_layer/mqtt_service.h"
#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <functional>

class SensorMonitor {
public:
    // GPIO监听配置
    struct GPIOCfg {
        std::string gpio_chip;    // GPIO芯片路径，如 "/dev/gpiochip0"
        int gpio_offset;          // GPIO偏移量
        std::string trigger_type; // 触发类型："rising", "falling", "both"
        std::string sensor_type;  // 传感器类型
        std::string sensor_id;    // 传感器ID
    };

public:
    SensorMonitor();
    ~SensorMonitor();
    
    bool initialize();
    void start();
    void stop();
    bool isRunning() const { return running_; }
    
    // GPIO管理
    bool addGPIOSensor(const GPIOCfg& config);
    bool removeGPIOSensor(const std::string& sensor_id);
    
private:
    void initializeSensors();
    void initializeVideo();
    void initializeMQTT();
    void initializeInteractions();
    
    // GPIO监听线程
    void gpioMonitoringThread();
    bool setupGPIOListener(const GPIOCfg& config);
    void handleGPIOEvent(const GPIOCfg& config, bool triggered);
    
    // 传感器配置
    void loadSensorConfig();
    
private:
    // 核心组件
    std::unique_ptr<SensorPollingReader> sensor_reader_;
    std::shared_ptr<SensorService> sensor_service_;
    std::shared_ptr<VideoService> video_service_;
    std::shared_ptr<SensorVideoInteractionService> interaction_service_;
    std::shared_ptr<MQTTService> mqtt_service_;
    
    // GPIO监听相关
    std::map<std::string, GPIOCfg> gpio_sensors_;  // sensor_id -> config
    std::thread gpio_monitor_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // GPIO文件描述符管理
    std::map<std::string, int> gpio_fds_;  // sensor_id -> file descriptor
};

#endif