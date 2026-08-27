#include "common/config/config_manager.h"

#include "common/log/log_manager.h"

#include <fstream>

using json = nlohmann::json;

ConfigManager::ConfigManager()
{

}

ConfigManager::~ConfigManager()
{

}

bool ConfigManager::load(
    const std::string& path
)
{

    std::ifstream file(path);


    if(!file.is_open())
    {
        Logger::error(
            "[Config] open config file failed"
        );

        return false;
    }



    json root;


    try
    {

        file >> root;

    }
    catch(const std::exception& e)
    {

        Logger::error(
            "[Config] json parse failed"
        );

        return false;

    }

    /*
        parse box
    */

    if(!parseBox(root))
    {
        Logger::error(
            "[Config] parse box failed"
        );

        return false;
    }



    Logger::info(
        "[Config] load successful"
    );


    return true;

}

const BoxConfig& ConfigManager::getConfig() const
{

    return config_;

}

const std::vector<MachineConfig>& 
ConfigManager::getMachines() const
{

    return config_.machines;

}

const MachineConfig*
ConfigManager::getMachine(
    const std::string& id
) const
{

    for(const auto& machine :
        config_.machines)
    {

        if(machine.id == id)
        {
            return &machine;
        }

    }


    return nullptr;

}

bool ConfigManager::parseBox(
    const json& root
)
{

    if(!root.contains("box"))
    {
        return false;
    }

    auto box = root["box"];

    if(box.contains("id"))
    {
        config_.id =
            box["id"].get<std::string>();
    }


    if(!root.contains("machines"))
    {
        return false;
    }

    for(auto& item : root["machines"])
    {

        MachineConfig machine;


        if(!parseMachine(
            item,
            machine
        ))
        {
            return false;
        }

        config_.machines.push_back(
            machine
        );

    }

    return true;

}

bool ConfigManager::parseMachine(
    const json& json,
    MachineConfig& machine
)
{

    if(!json.contains("id"))
    {
        return false;
    }



    machine.id =
        json["id"].get<std::string>();




    if(!json.contains("devices"))
    {
        return true;
    }




    for(auto& item : json["devices"])
    {

        DeviceConfig device;



        if(!parseDevice(
            item,
            device
        ))
        {
            return false;
        }



        machine.devices.push_back(
            device
        );

    }



    return true;

}

bool ConfigManager::parseDevice(
    const json& json,
    DeviceConfig& device
)
{

    if(!json.contains("id")
        ||
       !json.contains("type")
        ||
       !json.contains("model"))
    {
        return false;
    }



    device.id =
        json["id"].get<std::string>();


    device.type =
        json["type"].get<std::string>();


    device.model =
        json["model"].get<std::string>();

    if(json.contains("parameters"))
    {

        device.parameters =
            json["parameters"];

    }


    return true;

}
