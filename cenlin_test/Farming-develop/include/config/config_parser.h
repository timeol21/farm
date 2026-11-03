#pragma once

#include <string>
#include "config.h"
#include "utils/json.hpp"

class ConfigParser {
public:
    static ConfigParser& getInstance();

    bool loadConfig(const std::string& filePath = CONFIG_JSON);
    const AppConfig& getConfig() const;

private:
    ConfigParser() = default;
    bool parseChannels(const nlohmann::json& j);
    bool parseDevices(const nlohmann::json& j);
    bool parseMqtt(const nlohmann::json& j);
    bool parseSystem(const nlohmann::json& j);

private:
    AppConfig config_;
};