#include "ConfigManager.h"
#include <fstream>
#include <iostream>
#include <clocale>
#include <cerrno>

// 单例实例初始化
ConfigManager::ConfigManager() : isLoaded(false) {}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f.is_open()) {
        std::cerr << "错误：配置文件 " << filePath << " 打开失败（" << strerror(errno) << "）" << std::endl;
        return false;
    }

    try {
        f >> config;
        isLoaded = true;
        std::cout << "成功：配置文件加载完成" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "错误：配置文件解析失败 - " << e.what() << std::endl;
        isLoaded = false;
    }

    return isLoaded;
}

const json& ConfigManager::getConfig() const {
    if (!isLoaded) {
        throw std::runtime_error("配置文件未加载");
    }
    return config;
}

json ConfigManager::getSerialPortConfig(const std::string& portName) const {
    if (!isLoaded) return json();
    
    auto& serialPorts = config["system_config"]["serial_ports"];
    if (serialPorts.contains(portName)) {
        return serialPorts[portName];
    }
    return json({{"baud_rate", 9600}, {"read_timeout_ms", 1000}}); // 默认配置
}

int ConfigManager::getPollingInterval() const {
    if (!isLoaded) return 1000;
    return config.value("polling_interval_ms", 1000);
}

bool ConfigManager::findPLCComponentConfig(const std::string& compId, 
                                           std::string& plcId, 
                                           json& plcConfig, 
                                           json& compConfig) const {
    if (!isLoaded) return false;

    for (auto& plc : config["plc_devices"].items()) {
        auto& components = plc.value()["components"];
        if (components.contains(compId)) {
            plcId = plc.key();
            plcConfig = plc.value();
            compConfig = components[compId];
            return true;
        }
    }
    std::cerr << "错误：未找到PLC组件 " << compId << std::endl;
    return false;
}

bool ConfigManager::findSerialDirectConfig(const std::string& devId, json& devConfig) const {
    if (!isLoaded) return false;

    for (auto& dev : config["linux_direct_devices"]["serial_direct_devices"]) {
        if (dev["id"] == devId) {
            devConfig = dev;
            return true;
        }
    }
    std::cerr << "错误：未找到串口直连设备 " << devId << std::endl;
    return false;
}

bool ConfigManager::findGPIOConfig(const std::string& devId, json& devConfig) const {
    if (!isLoaded) return false;

    for (auto& dev : config["linux_direct_devices"]["gpio_devices"]) {
        if (dev["id"] == devId) {
            devConfig = dev;
            return true;
        }
    }
    std::cerr << "错误：未找到GPIO设备 " << devId << std::endl;
    return false;
}

void ConfigManager::initEncoding() {
    const char* locales[] = {"zh_CN.UTF-8","en_US.UTF-8","C.UTF-8","POSIX"};
    for (auto loc : locales) {
        if (setlocale(LC_ALL, loc) != NULL) {
            std::cout << "编码初始化成功: " << loc << std::endl;
            return;
        }
    }
    std::perror("所有编码设置均失败，可能导致打印乱码");
}