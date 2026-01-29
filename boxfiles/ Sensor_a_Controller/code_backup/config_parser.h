#pragma once

#include <string>
#include "config_info.h"
#include "json.hpp"   // nlohmann/json
#include <mutex>

class ConfigParser {
public:
    static ConfigParser& getInstance();

    bool loadFromFile(const std::string& path);
    const DeviceConfigRoot& getConfig() const { return config_; }

private:
    ConfigParser() = default;
    ConfigParser(const ConfigParser&) = delete;
    ConfigParser& operator=(const ConfigParser&) = delete;

    void parseCameras(const nlohmann::json& j);
    void parsePLCList(const nlohmann::json& j);
    void parsePLCDevices(const nlohmann::json& j);
    void parseSensors(const nlohmann::json& j);
    void parseCarControls(const nlohmann::json& j);
    void parseGateways(const nlohmann::json& j);

private:
    bool isLoaded_ = false;
    DeviceConfigRoot config_;
};
