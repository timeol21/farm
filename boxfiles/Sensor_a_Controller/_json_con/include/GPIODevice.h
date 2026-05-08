#ifndef GPIO_DEVICE_H
#define GPIO_DEVICE_H

#include "BaseDevice.h"
#include "GpioUtils.h"

class GPIODevice : public BaseDevice {
protected:
    GpioUtils gpio;             // GPIO工具
    int initialValue;           // 初始电平

public:
    GPIODevice(const std::string& deviceId);
    ~GPIODevice() override = default;

    // 初始化设备（查找GPIO配置+导出GPIO）
    bool init() override;

    // 控制GPIO电平
    bool controlGPIO(int value);

    // 交互式控制（通用逻辑）
    void interactiveControl();

    // 打印设备信息（通用逻辑）
    void printDeviceInfo() const override;

    // 读取数据（GPIO设备默认空实现，子类可重写）
    bool readData() override { return true; }
};

#endif // GPIO_DEVICE_H