#pragma once

#include <string>

#include <vector>

#include <nlohmann/json.hpp>

#include "common/config/box_config.h"



class ConfigManager
{

public:

    ConfigManager();

    ~ConfigManager();

    /*
        加载配置文件

        path:
        config.json路径

    */

    bool load(const std::string& path);

    /*
        获取完整配置

    */

    const BoxConfig& getConfig() const;
    
    const std::vector<MachineConfig>& getMachines() const;

    const MachineConfig* getMachine(const std::string& id) const;
    
private:

    bool parseBox(const nlohmann::json& json);

    bool parseMachine(const nlohmann::json& json,MachineConfig& machine);

    bool parseDevice(const nlohmann::json& json,DeviceConfig& device);

    /*
        整个系统配置

    */

    BoxConfig config_;


};