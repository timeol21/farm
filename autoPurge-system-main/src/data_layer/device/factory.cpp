#include "data_layer/device/driver/factory.h"
#include "data_layer/device/runtime/factory.h"
std::string DriverResolver::resolveDriverType(const DeviceNode& node) const{
    return ""; 
}


std::shared_ptr<IDeviceDriver> DriverFactory::createDriver(const DeviceNode& node,const DeviceAccessPath& path,SdkEnvironmentManager& sdkManager){
    return nullptr;
}



std::shared_ptr<IDeviceDriver> DriverFactory::createDeviceDriver(const DeviceNode& node,SdkEnvironmentManager& sdkManager){
         const auto& def = node.definition;


    // 1. 海康 NVR
    if (def.deviceTypeEnum == DeviceType::NVR && def.vendor == "hikvision") {
        VendorSdkType type = VendorSdkType::Hikvision;
        auto sdk_env = sdkManager.get(type);
        return std::make_shared<HikvisionNvrDriver>(sdk_env,node );
    }

    // 2. 网络相机（RTSP）
    if (def.deviceTypeEnum == DeviceType::CAMERA) {
        VendorSdkType type = VendorSdkType::RTSP;
        auto sdk_env = sdkManager.get(type);
        return std::make_shared<RtspFFmpegDriver>(sdk_env ,node );
    }

    // 3. PLC 且 以太网口 → ModbusTCP
    if (def.deviceTypeEnum == DeviceType::PLC && node.interface->type == InterfaceType::Ethernet) {
        // VendorSdkType type = VendorSdkType::Modbus;
        // auto sdk_env = sdkManager.get(type);sdk_env,
        return std::make_shared<ModbusTcpDriver>(node );
    }

    // 4. 串口/RS485 传感器/PLC → ModbusRTU
    if (
        (def.deviceTypeEnum == DeviceType::PLC || def.deviceTypeEnum == DeviceType::SENSOR) &&
        (node.interface->type == InterfaceType::RS485 || node.interface->type == InterfaceType::Serial)
    ) {
        // VendorSdkType type = VendorSdkType::Modbus;
        // auto sdk_env = sdkManager.get(type); sdk_env,
        return getOrCreate(*node.interface);
    }

    // 不支持的设备
//     std::cerr << "不支持的设备类型: " << def.id << std::endl;
    return nullptr;
}

std::shared_ptr<DeviceRuntime> DeviceRuntimeFactory::createRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<RuntimeContext>& context) {
    const auto& def = node->definition;

    // ====================== 安全校验 ======================
    // if (!node.interface) {
    //     throw std::invalid_argument("设备节点缺少接口定义，无法创建Runtime");
    // }
    if (def.bindingMode == RuntimeBindingMode::Dependent && node->parent.expired()) {
        throw std::invalid_argument(std::string("依附型设备缺少父节点：" + def.id));
    }

    // // ====================== 1. NVR 通道（依附型）
    // if (def.deviceTypeEnum == DeviceType::NVR &&def.role == DeviceRole::LogicalPoint &&def.vendor == "hikvision")
    // {
    //     return std::make_shared<HikvisionNvrChannelRuntime>(node, driver, context);
    // }

    // // ====================== 2. NVR 设备
    // if (def.deviceTypeEnum == DeviceType::NVR && def.vendor == "hikvision") {
    //     return std::make_shared<HikvisionNvrRuntime>(node, driver, context);
    // }

    // // ====================== 3. 海康摄像头
    // if (def.deviceTypeEnum == DeviceType::CAMERA && def.vendor == "hikvision_channel") {
    //     return std::make_shared<HikvisionCameraRuntime>(node, driver, context);
    // }

    // ====================== 4. 通用摄像头（USB）
    if (def.deviceTypeEnum == DeviceType::CAMERA) {
        return std::make_shared<CameraRuntime>(node, driver, context->frameBuffer);
    }

    // ====================== 5. PLC
    if (def.deviceTypeEnum == DeviceType::PLC ||
        def.deviceTypeEnum == DeviceType::PLC_DEVICE)
    {
        return std::make_shared<PlcRuntime>(node, driver);
    }

    // // ====================== 6. 网关（边缘盒子、平台）
    // if (def.role == DeviceRole::Gateway) {
    //     return std::make_shared<GatewayRuntime>(node, driver, context);
    // }

    // ====================== 7. 雷达
    if (def.deviceTypeEnum == DeviceType::RADAR) {
        return std::make_shared<RadarRuntime>(node, driver);
    }

    // ====================== 8. 传感器
    if (def.deviceTypeEnum == DeviceType::SENSOR || def.role == DeviceRole::Sensor) {
        return std::make_shared<SensorRuntime>(node, driver);
    }

    // ====================== 9. 执行器
    if (def.role == DeviceRole::Actuator) {
        return std::make_shared<ActuatorRuntime>(node, driver);
    }

    // 未知设备
    return nullptr;
}


std::shared_ptr<IDeviceDriver> DriverFactory::getOrCreate(const InterfaceDefinition& iface){

    std::string key = iface.endpoint;

    auto it = drivers_.find(key);
    if (it != drivers_.end()) {
        return it->second;
    }

    auto driver = std::make_shared<ModbusRtuDriver>(iface);
    drivers_[key] = driver;
    return driver;

}

