#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "isensor.h"
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

// 红外传感器1
class InfraredSensor : public ISensor {
public:
    InfraredSensor(const std::string& id, int gpio_pin);
    bool initialize() override;
    std::unique_ptr<SensorData> readData() override;
    std::string getType() const override { return "infrared"; }
    std::string getId() const override { return id_; }
    bool isAvailable() const override;
    
private:
    std::string id_;
    int gpio_pin_;
    bool is_initialized_;
};

// 水浸传感器
class WaterImmersionSensor : public ISensor {
public:
    WaterImmersionSensor(const std::string& id, int gpio_pin);
    bool initialize() override;
    std::unique_ptr<SensorData> readData() override;
    std::string getType() const override { return "water_immersion"; }
    std::string getId() const override { return id_; }
    bool isAvailable() const override;
    
private:
    std::string id_;
    int gpio_pin_;
    bool is_initialized_;
};

// 烟雾传感器
class SmokeSensor : public ISensor {
public:
    SmokeSensor(const std::string& id, int gpio_pin);
    bool initialize() override;
    std::unique_ptr<SensorData> readData() override;
    std::string getType() const override { return "smoke"; }
    std::string getId() const override { return id_; }
    bool isAvailable() const override;
    
private:
    std::string id_;
    int gpio_pin_;
    bool is_initialized_;
};

// 传感器轮询读取器
class SensorPollingReader : public ISensorListener {
public:
    SensorPollingReader();
    ~SensorPollingReader();
    
    void addSensor(std::unique_ptr<ISensor> sensor);
    void setListener(std::shared_ptr<ISensorListener> listener);
    void startPolling(int interval_ms = 1000);
    void stopPolling();
    bool isPolling() const { return running_; }
    
    // ISensorListener implementation
    void onSensorData(const SensorData& data) override;
    
private:
    void pollingLoop();
    
    std::vector<std::unique_ptr<ISensor>> sensors_;
    std::shared_ptr<ISensorListener> external_listener_;
    std::thread polling_thread_;
    std::atomic<bool> running_{false};
    int polling_interval_;
};

#endif