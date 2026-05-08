#ifndef BASE_DEVICE_H
#define BASE_DEVICE_H

#include <string>
#include <nlohmann/json.hpp>
#include "ConfigManager.h"

using json = nlohmann::json;

class BaseDevice {
protected:
    ConfigManager& configMgr;   // 配置管理器（单例）
    std::string deviceId;       // 设备ID
    json deviceConfig;          // 设备配置
    bool isInitialized;         // 是否初始化成功

public:
    BaseDevice(const std::string& deviceId);
    virtual ~BaseDevice() = default;

    // 初始化设备（纯虚函数，子类实现）
    virtual bool init() = 0;

    // 启动轮询（通用逻辑）
    void startPolling();

    // 读取数据（纯虚函数，子类实现差异化逻辑）
    virtual bool readData() = 0;

    // 打印设备信息（纯虚函数，子类实现）
    virtual void printDeviceInfo() const = 0;

    // 检查是否初始化成功
    bool isInitSuccess() const;
};

#endif // BASE_DEVICE_H