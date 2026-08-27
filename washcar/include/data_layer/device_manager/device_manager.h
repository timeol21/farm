#pragma once
#include <memory>
class ConfigManager;

class DeviceFactory;

class CameraManager;

class PlcManager;

class Camera;

class Plc;

class DeviceManager
{

public:

    DeviceManager();

    ~DeviceManager();

    bool initialize();
    
    bool start();


    void stop();

    CameraManager& camera();

    PlcManager& plc();
  

private:

    bool createDevices();

    std::unique_ptr<ConfigManager> configManager_;


    std::unique_ptr<DeviceFactory> deviceFactory_;


    std::unique_ptr<CameraManager> cameraManager_;


    std::unique_ptr<PlcManager> plcManager_;


};