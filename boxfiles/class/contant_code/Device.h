#ifndef DEVICE_H
#define DEVICE_H

#include <string>

class Device {
public:
    std::string deviceName;
    std::string portPath;

    Device(std::string name, std::string port);
    virtual ~Device();

    // 所有的设备都要实现这个执行函数
    virtual void execute() = 0;
};

#endif