#ifndef ISENSOR_H
#define ISENSOR_H

#include <string>
#include <memory>
#include <vector>

// 传感器数据基类
struct SensorData {
    std::string type;
    std::string id;
    long timestamp;
    virtual ~SensorData() = default;
};

// 红外传感器数据
struct InfraredSensorData : public SensorData {
    bool is_triggered;
    float distance;
};

// 水浸传感器数据
struct WaterImmersionSensorData : public SensorData {
    bool is_immersed;
    float water_level;
};

// 烟雾传感器数据
struct SmokeSensorData : public SensorData {
    bool smoke_detected;
    float smoke_density;
};

// 传感器接口
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual bool initialize() = 0;
    virtual std::unique_ptr<SensorData> readData() = 0;
    virtual std::string getType() const = 0;
    virtual std::string getId() const = 0;
    virtual bool isAvailable() const = 0;
};

// 传感器监听器接口
class ISensorListener {
public:
    virtual ~ISensorListener() = default;
    virtual void onSensorData(const SensorData& data) = 0;
};

#endif