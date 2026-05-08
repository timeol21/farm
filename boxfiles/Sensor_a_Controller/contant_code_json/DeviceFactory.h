#ifndef DEVICE_FACTORY_H
#define DEVICE_FACTORY_H

#include "Device.h"
#include <string>

class DeviceFactory {
public:
    // 简单工厂方法：你给类型字符串，我给设备对象
    static Device* createDevice(std::string kind, std::string id, std::string name, std::string port, int addr = 0);
};

#endif