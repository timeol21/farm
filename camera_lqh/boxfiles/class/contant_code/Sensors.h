#ifndef SENSORS_H
#define SENSORS_H

#include "Device.h"
#include "SerialManager.h"

// 烟雾传感器
class SmokeSensor : public Device {
    SerialManager sm;
public:
    using Device::Device; // 使用基类构造函数
    void execute() override;
};

// 水位传感器
class WaterSensor : public Device {
    SerialManager sm;
public:
    using Device::Device;
    void execute() override;
};

// 温湿度传感器
class HumitureSensor : public Device {
    SerialManager sm;
public:
    using Device::Device;
    void execute() override;
};

#endif