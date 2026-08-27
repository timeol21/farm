#include "data_layer/device_manager/device_factory.h"


#include "data_layer/camera/camera_factory.h"

#include "data_layer/plc/plc_factory.h"


#include "data_layer/camera/camera.h"

#include "data_layer/plc/plc.h"

#include "common/log/log_manager.h"

DeviceFactory::DeviceFactory()
{

    cameraFactory_ =
        std::make_unique<CameraFactory>();


    plcFactory_ =
        std::make_unique<PlcFactory>();

}

DeviceFactory::~DeviceFactory()
{

}

std::unique_ptr<Camera>
DeviceFactory::createCamera(
    const DeviceConfig& config
)
{

    if(!cameraFactory_)
    {
        return nullptr;
    }


    auto camera =
    cameraFactory_->create(
        config
    );


    if(!camera)
    {
        Logger::error(
            "[DeviceFactory] create camera failed: "
            + config.id
        );
    
    }

    return camera;

}

std::unique_ptr<Plc>
DeviceFactory::createPlc(
    const DeviceConfig& config
)
{

    if(!plcFactory_)
    {
        return nullptr;
    }


    auto plc =
        plcFactory_->create(
            config
        );


    if(!plc)
    {
        Logger::error(
            "[DeviceFactory] create plc failed: "
            + config.id
        );
    }
    
    return plc;

}