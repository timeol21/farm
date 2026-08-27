#pragma once

#include <memory>

#include "data_layer/plc/plc.h"

#include "common/config/device_config.h"

class PlcFactory
{

public:

    PlcFactory();

    ~PlcFactory();

public:


    std::unique_ptr<Plc> create(
        const DeviceConfig& config
    );


};