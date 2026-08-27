#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

class DeviceConfig
{

public:

    DeviceConfig() = default;

    ~DeviceConfig() = default;


public:

    std::string id;


    // device type
    // camera / plc
    std::string type;


    // device model
    // icsee / hik / fx3u
    std::string model;


    /*
        保存设备参数

        不在这里区分camera参数还是plc参数

        因为DeviceConfig只是配置数据

        具体解释由对应Factory完成
    */
    nlohmann::json parameters;


};