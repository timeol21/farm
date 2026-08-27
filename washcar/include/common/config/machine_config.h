#pragma once

#include <string>
#include <vector>

#include "common/config/device_config.h"


class MachineConfig
{

public:

    MachineConfig() = default;


    ~MachineConfig() = default;



public:


    // wash machine id

    std::string id;



    // devices belong to this machine

    std::vector<DeviceConfig> devices;


};