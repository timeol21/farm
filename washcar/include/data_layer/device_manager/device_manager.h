#pragma once
#include <memory>
class CameraManager;
class PlcManager;

class DeviceManager
{

public:

    DeviceManager();

    ~DeviceManager();

    bool initialize();

    CameraManager& camera();

    PlcManager& plc();


private:


    DeviceManager(const DeviceManager&) = delete;

    DeviceManager& operator=(const DeviceManager&) = delete;

    std::unique_ptr<CameraManager> cameraManager_;

    std::unique_ptr<PlcManager> plcManager_;

};