#ifndef DEVICE_FACTORY_H
#define DEVICE_FACTORY_H

#include "Device.h"
#include <string>

class DeviceFactory {
public:
    // 传感器工厂：根据类型名创建（Smoke, Water, Humiture）
    static Device* createSensor(std::string type, std::string name, std::string port);

    // 控制器工厂：专门创建电磁阀
    static Device* createController(std::string name, std::string port, int addr);
};

#endif