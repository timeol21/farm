#pragma once

#include <string>
#include "config_info.h"
#include "json.hpp"   // nlohmann/json
#include <mutex>
#include <climits>

class ConfigParser {
private:
    ConfigParser() = default;  // 单例私有构造
    GlobalConfig config_;
    bool isLoaded_ = false;
    
private:
    
    void parseNVR(const nlohmann::json& j);  // j 直接是 device_config 下的 nvr 节点
    void parseCameras(const nlohmann::json& j); // j 是 device_config.devices 节点
    void CameraChangeChannelNo(std::vector<CameraConfig>& cameras);
    int cameraIdToInt(const std::string& cameraId);
    void parseGPIOSensors(const nlohmann::json& j);
    void parseTempHumiditySensor(const nlohmann::json& j);
    void parseDoorLocks(const nlohmann::json& j);
public:   
    static ConfigParser& getInstance();
    // 获取配置的接口
    const GlobalConfig& getConfig() const { return config_; }
    bool loadFromFile(const std::string& path);  
};
