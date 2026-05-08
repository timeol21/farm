#pragma once

#include "common/config/json_util.h"
#include "common/config/config_object.h"
#include "common/log/log_manager.h"
#include <vector>
#include <iostream>
class SystemConfig{
public:
    // 1. 删除拷贝构造、赋值运算符 → 禁止复制
    SystemConfig(const SystemConfig&) = delete;
    SystemConfig& operator=(const SystemConfig&) = delete;

    // 2. 单例全局获取接口（C++11 线程安全）
    static SystemConfig& instance();
    
    bool init(const std::string& path);

    bool load(const std::string& path);

    std::string getVersion() const;
    std::string getDescription() const;
    const SystemInfo& getSystem() const;
    const Services& getServices() const;
    const Logging& getLogging() const;
    const DeviceServiceConfig& getDeviceServiceConfig() const;
    
private:
    SystemConfig();
    ~SystemConfig() = default;
    void parse(const json& j);
    bool initDeviceConfig();
private:
    bool m_initialized; 
    std::string version;
    std::string description;
    SystemInfo system;
    Services services;
    Logging logging;   
};


