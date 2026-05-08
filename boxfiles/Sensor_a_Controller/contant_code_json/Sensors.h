#ifndef SENSORS_H
#define SENSORS_H

#include "Device.h"
#include "SerialManager.h"

// 1. 烟雾传感器 (对应 X1 节点)
class SmokeSensor : public Device {
public:
    SerialManager sm; 
    SmokeSensor();
    void execute() override; // 重写父类执行函数
};

// 2. 水位传感器 (对应 X0 节点)
class WaterSensor : public Device {
public:
    SerialManager sm;
    WaterSensor();
    void execute() override;
};

// 3. 温湿度传感器 (USB 485)
class HumitureSensor : public Device {
public:
    SerialManager sm;
    float currentTemp;
    float currentHumi;

    HumitureSensor();
    void execute() override;
};

#endif