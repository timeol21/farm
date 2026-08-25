#include "data_layer/device_manager/device_manager.h"

#include "data_layer/camera/camera_manager.h"
#include "data_layer/plc/plc_manager.h"

DeviceManager::DeviceManager()
{

}


DeviceManager::~DeviceManager()
{

}


bool DeviceManager::initialize()
{

    cameraManager_ = std::make_unique<CameraManager>();

    plcManager_ = std::make_unique<PlcManager>();

    if(!cameraManager_->initialize())
    {
        return false;
    }

    if(!plcManager_->initialize())
    {
        return false;
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