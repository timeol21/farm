#pragma once


#include <memory>


#include "common/config/device_config.h"



class Camera;

class Plc;



class CameraFactory;

class PlcFactory;



class DeviceFactory
{

public:


    DeviceFactory();


    ~DeviceFactory();



public:


    std::unique_ptr<Camera> createCamera(
        const DeviceConfig& config
    );



    std::unique_ptr<Plc> createPlc(
        const DeviceConfig& config
    );



private:


    std::unique_ptr<CameraFactory> cameraFactory_;


    std::unique_ptr<PlcFactory> plcFactory_;


};