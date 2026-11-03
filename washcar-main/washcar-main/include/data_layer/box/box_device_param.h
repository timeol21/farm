#ifndef BOX_DEVICE_PARAM_H
#define BOX_DEVICE_PARAM_H

#include "data_layer/box/box_config.h"
#include "data_layer/camera/camera_config.h"
//#include "data_layer/plc_device/solenoid_config.h"
#include <vector>

class BoxDeviceParam {
    public:

        BoxDeviceParam() = default;
        ~BoxDeviceParam() = default;
    
    private:
        BoxConfig boxConfig;
        //std::vector<SolenoidConfig> solenoidConfigs;
        std::vector<CameraConfig> cameraConfigs;
};

#endif