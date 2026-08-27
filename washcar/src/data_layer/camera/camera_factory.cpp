#include "data_layer/camera/camera_factory.h"

#include "data_layer/camera/camera.h"

#include "data_layer/camera/icsee_camera/icsee_camera.h"

#include "common/log/log_manager.h"

CameraFactory::CameraFactory()
{

}

CameraFactory::~CameraFactory()
{

}


std::unique_ptr<Camera> CameraFactory::create(
    const DeviceConfig& config
)
{

    if(config.type != "camera")
    {
        Logger::error(
            "[Camera Factory] invalid device type"
        );
    
        return nullptr;
    }
    
    if(config.model == "icsee")
    {

        Logger::info(
            "[Factory] create icsee camera"
        );


        return std::make_unique<IcSeeCamera>(
            config
        );

    }



    if(config.model == "hik")
    {

        Logger::info(
            "[Factory] create hik camera"
        );


        // 后续实现
        // return std::make_unique<HikCamera>(config);
        return nullptr;

    }



    Logger::error(
        "[Factory] unknown camera model: "
        + config.model
    );


    return nullptr;

}