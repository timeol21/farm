#ifndef BOX_INSTANCE_H
#define BOX_INSTANCE_H

#include "data_layer/box/box_device_param.h"
#include "data_layer/box/box_config_result.h"
class BoxInstance {
    public:
        BoxInstance() = default;
        ~BoxInstance();

        BoxConfigResult configBoxDeviceParams(const BoxDeviceParam& params);

    private:
        //盒子相关配置
};

#endif
