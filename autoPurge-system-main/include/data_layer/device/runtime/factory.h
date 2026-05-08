#pragma once
#include "data_layer/device/runtime/device_runtime.h"
#include "data_layer/device/driver/factory.h"

class DeviceRuntimeFactory{
public:
    std::shared_ptr<DeviceRuntime> createRuntime(
        std::shared_ptr<DeviceNode> node, 
        std::shared_ptr<IDeviceDriver> driver,
        std::shared_ptr<RuntimeContext>& context
    );

private:
    // std::shared_ptr<DeviceRuntime> createHikvisionNvrRuntime(std::shared_ptr<IDeviceDriver> driver);

    // std::shared_ptr<DeviceRuntime> createHikvisionCameraRuntime(std::shared_ptr<IDeviceDriver> driver);

    // std::shared_ptr<DeviceRuntime> createHikvisionNvrChannelRuntime(std::shared_ptr<IDeviceDriver> driver, const std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>>& existingRuntimes);

};