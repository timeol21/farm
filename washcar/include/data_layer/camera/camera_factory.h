#pragma once


#include <memory>


#include "common/config/device_config.h"



class Camera;



class CameraFactory
{

public:


    CameraFactory();


    ~CameraFactory();



public:


    std::unique_ptr<Camera> create(
        const DeviceConfig& config
    );


};