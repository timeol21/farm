#ifndef SERIAL_DIRECT_DEVICE_H
#define SERIAL_DIRECT_DEVICE_H

#include "BaseDevice.h"
#include "SerialUtils.h"

class SerialDirectDevice : public BaseDevice {
protected:
    SerialUtils serial;         // 串口工具
    std::string serialPort;     // 绑定的串口

public:
    SerialDirectDevice(const std::string& deviceId);
    ~SerialDirectDevice() override = default;

    // 初始化设备（查找直连配置+初始化串口）
    bool init() override;

    // 打印设备信息（通用逻辑）
    void printDeviceInfo() const override;
};

#endif // SERIAL_DIRECT_DEVICE_H