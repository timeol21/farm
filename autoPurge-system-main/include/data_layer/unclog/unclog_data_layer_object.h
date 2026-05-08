#pragma once
#include <vector>
#include "common/config/config_object.h"
struct RawDeviceInfo{

    SystemInfo systemInfo;

    std::vector<CameraConfig>  cameras;
    
    std::vector<PlcDeviceConfig> plcDevices;

    std::vector<SensorConfig> sensors;

    DeviceServiceConfig device_config;
};