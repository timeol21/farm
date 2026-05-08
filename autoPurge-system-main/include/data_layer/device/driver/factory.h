#pragma once
#include "data_layer/device/driver/device_driver.h"
#include "data_layer/device/topology/access_path.h"
#include "data_layer/device/vendor_sdk/sdk_environment_manager.h"
#include "data_layer/device/driver/driver_manager.h"

class DriverResolver {
public:
    std::string resolveDriverType(const DeviceNode& node) const;
};


class DriverFactory {
public:
    DriverFactory(DriverManager& driverManager) :driverManager_(driverManager){}

    ~DriverFactory() = default;


    std::shared_ptr<IDeviceDriver> createDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);
    
    std::shared_ptr<IDeviceDriver> createDeviceDriver(const DeviceNode& node,SdkEnvironmentManager& sdkManager);

private:

    // 辅助函数：DeviceType 转字符串
    static std::string deviceTypeToString(DeviceType type) {
        switch (type) {
            case DeviceType::PLC: return "PLC";
            case DeviceType::SENSOR: return "SENSOR";
            case DeviceType::CAMERA: return "CAMERA";
            case DeviceType::NVR: return "NVR";
            case DeviceType::GPIO: return "GPIO";
            case DeviceType::RADAR: return "RADAR";
            case DeviceType::PLC_DEVICE: return "PLC_DEVICE";
            case DeviceType::UNKNOWN: return "UNKNOWN";
            default: return "UNKNOWN";
        }
    }
    // std::shared_ptr<IDeviceDriver> createUsbTempSensorDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);

    // std::shared_ptr<IDeviceDriver> createSiemensPlcDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);

    // std::shared_ptr<IDeviceDriver> createPlcValvePointDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);

    // std::shared_ptr<IDeviceDriver> createHikvisionNvrDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);

    // std::shared_ptr<IDeviceDriver> createNvrCameraChannelDriver( const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager);

    std::shared_ptr<IDeviceDriver> getOrCreate(const InterfaceDefinition& iface);

private:
    std::unordered_map<std::string, std::shared_ptr<IDeviceDriver>> drivers_;

    DriverManager& driverManager_;

};