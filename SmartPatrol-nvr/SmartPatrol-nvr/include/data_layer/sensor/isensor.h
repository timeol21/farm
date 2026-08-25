#ifndef ISENSOR_H
#define ISENSOR_H

#include <string>
#include <memory>
#include <cstdint>

// 传感器数据结构1
struct SensorData {
    std::string sensor_id;      // 传感器ID
    std::string sensor_type;    // 传感器类型: "infrared", "water_immersion", "smoke"
    bool state;                 // 触发状态: true=触发, false=未触发
    long timestamp;             // 时间戳(毫秒)
    
    SensorData() : state(false), timestamp(0) {}
    
    SensorData(const std::string& id, const std::string& type, bool st, long ts)
        : sensor_id(id), sensor_type(type), state(st), timestamp(ts) {}
};

// 传感器接口
class ISensor {
public:
    virtual ~ISensor() = default;
    
    // 初始化传感器
    virtual bool initialize() = 0;
    
    // 读取传感器数据
    virtual std::unique_ptr<SensorData> readData() = 0;
    
    // 获取传感器类型
    virtual std::string getType() const = 0;
    
    // 获取传感器ID
    virtual std::string getId() const = 0;
    
    // 检查传感器是否可用
    virtual bool isAvailable() const = 0;
};

// 传感器数据监听器接口
class ISensorListener {
public:
    virtual ~ISensorListener() = default;
    
    // 当收到传感器数据时调用
    virtual void onSensorData(const SensorData& data) = 0;
};

#endif // ISENSOR_H
