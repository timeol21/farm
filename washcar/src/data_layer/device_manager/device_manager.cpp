#include "common/log/log_manager.h"

#include "common/config/config_manager.h"

#include "data_layer/device_manager/device_factory.h"

#include "data_layer/device_manager/device_manager.h"

#include "data_layer/camera/camera_manager.h"

#include "data_layer/plc/plc_manager.h"

DeviceManager::DeviceManager()
{

}


DeviceManager::~DeviceManager()
{
    stop();
}


bool DeviceManager::initialize()
{
    configManager_ = std::make_unique<ConfigManager>();


    deviceFactory_ = std::make_unique<DeviceFactory>();
    
    cameraManager_ = std::make_unique<CameraManager>();

    plcManager_ = std::make_unique<PlcManager>();
    
    if(!configManager_->load(
        "../include/common/config/config.json"
        //"/home/ztl/program/washcar/include/common/config/config.json"
    ))
    {

        Logger::error(
            "[DeviceManager] load config failed"
        );

        return false;

    }



    if(!createDevices())
    {

        Logger::error(
            "[DeviceManager] create devices failed"
        );

        return false;

    }

    if(!cameraManager_->initialize())
    {
        Logger::error("[System] cameraManager initialize failed");
        return false;
    }else{
        Logger::info("[System] cameraManager initialize successful");
    }

    if(!plcManager_->initialize())
    {
        Logger::error("[System] plcManager initialize failed");
        return false;
    }else{
        Logger::info("[System] plcManager initialize successful");
    }

    return true;

}

bool DeviceManager::start()
{

    if(!plcManager_->start())
    {

        Logger::error(
            "[DeviceManager] plc start failed"
        );

        return false;

    }



    if(!cameraManager_->start())
    {

        Logger::error(
            "[DeviceManager] camera start failed"
        );

        return false;

    }



    return true;

}

void DeviceManager::stop()
{

    if(cameraManager_)
    {

        cameraManager_->stop();

    }



    if(plcManager_)
    {

        plcManager_->stop();

    }

}

bool DeviceManager::createDevices()
{

    const auto& config =
        configManager_->getConfig();



    for(auto& machine :
        config.machines)
    {


        for(auto& device :
            machine.devices)
        {



            if(device.type == "camera")
            {


                auto camera =
                    deviceFactory_->createCamera(
                        device
                    );


                if(!camera)
                {

                    Logger::error(
                        "[DeviceManager] create camera failed"
                    );

                    return false;

                }


                cameraManager_->addCamera(
                    std::move(camera)
                );


            }


            else if(device.type == "plc")
            {


                auto plc =
                    deviceFactory_->createPlc(
                        device
                    );


                if(!plc)
                {

                    Logger::error(
                        "[DeviceManager] create plc failed"
                    );

                    return false;

                }


                plcManager_->addPlc(
                    std::move(plc)
                );


            }


            else
            {


                Logger::error(
                    "[DeviceManager] unknown device type"
                );


                return false;

            }


        }


    }


    return true;

}

CameraManager& DeviceManager::camera()
{

    return *cameraManager_;

}



PlcManager& DeviceManager::plc()
{

    return *plcManager_;

}