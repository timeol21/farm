#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ConfigManager {
private:
    json config;                // 全局配置
    bool isLoaded;              // 配置是否加载成功

    // 私有构造函数（单例模式）
    ConfigManager();
    // 禁止拷贝
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

public:
    // 获取单例实例
    static ConfigManager& getInstance();

    // 加载配置文件
    bool loadConfig(const std::string& filePath = "config.json");

    // 获取完整配置
    const json& getConfig() const;

    // 获取串口配置
    json getSerialPortConfig(const std::string& portName) const;

    // 获取轮询间隔（ms）
    int getPollingInterval() const;

    // 查找PLC组件设备配置
    bool findPLCComponentConfig(const std::string& compId, 
                                std::string& plcId, 
                                json& plcConfig, 
                                json& compConfig) const;

    // 查找串口直连设备配置
    bool findSerialDirectConfig(const std::string& devId, json& devConfig) const;

    // 查找GPIO设备配置
    bool findGPIOConfig(const std::string& devId, json& devConfig) const;

    // 初始化编码（统一复用）
    static void initEncoding();
};

#endif // CONFIG_MANAGER_H