#ifndef PLC_COMPONENT_DEVICE_H
#define PLC_COMPONENT_DEVICE_H

#include <string>
#include "BaseDevice.h"
#include "SerialUtils.h"

class PLCComponentDevice : public BaseDevice {
protected:
    SerialUtils serial;         // 串口工具
    std::string plcId;          // PLC ID
    std::string serialPort;     // 绑定的串口
    json plcConfig;             // PLC配置

public:
    PLCComponentDevice(const std::string& deviceId);
    ~PLCComponentDevice() override = default;

    // 初始化设备（查找PLC配置+初始化串口）
    bool init() override;

    // 打印设备信息（通用逻辑）
    void printDeviceInfo() const override;
};

#endif // PLC_COMPONENT_DEVICE_H