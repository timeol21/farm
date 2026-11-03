#include "device/device_factory.h"
#include "actuator/solenoid.h"
#include "logger/logger.h"

namespace {
ConnectType resolveConnectType(const DeviceConfig& cfg) {
    if (cfg.channelId.rfind("RS485", 0) == 0 || cfg.channelId.rfind("SERIAL", 0) == 0) {
        return ConnectType::DIRECT;
    }
    return ConnectType::DIRECT;
}

DeviceType resolveDeviceType(const std::string& type) {
    if (type == "sensor") return DeviceType::SENSOR;
    if (type == "position") return DeviceType::POSITION;
    if (type == "camera") return DeviceType::CAMERA;
    return DeviceType::ACTUATOR;
}

ProtocolType resolveProtocolType(const std::string& protocol) {
    if (protocol == "modbus") return ProtocolType::MODBUS;
    if (protocol == "mqtt") return ProtocolType::MQTT;
    if (protocol == "tcpip") return ProtocolType::TCPIP;
    if (protocol == "http") return ProtocolType::HTTP;
    return ProtocolType::OTHER;
}
}

std::unique_ptr<IDevice> DeviceFactory::createDevice(DeviceConfig cfg) {
    try {
        const std::string& deviceId = cfg.id;
        const std::string& type = cfg.type;
        const std::string& model = cfg.model;

        

        DeviceState baseState(
            cfg.id,
            resolveDeviceType(type),
            resolveConnectType(cfg),
            cfg.channelId,
            resolveProtocolType(cfg.protocol)
        );
        baseState.online = false;
        baseState.statusCode = DeviceStatusCode::OFFLINE;

        std::unique_ptr<IDevice> device;

        // ====================== 传感器 ======================
        if (type == "sensor") {
            if (model == "temp") {
                // device = std::make_unique<GenericSensor>()); // 参数自己填，这是示例
            }
        }

        // ====================== UWB 定位设备 ======================
        else if (type == "position") {
            if (model == "uwb") {
                // device = std::make_unique<UwbDevice>()); //同上
            }
        }

        // ======================= 执行器 ======================
        else if (type == "actuator") {
            if (model == "valve") {
                device = std::make_unique<Solenoid>(cfg,baseState); //同上
            }
        }

        if (!device) {
            LOG_ERROR("[DeviceFactory] Unsupported device type: " + type + " model:" + model);
            return nullptr;
        }

        return device;
    }
    catch (const std::exception& e) {
        LOG_ERROR("[DeviceFactory] Crash: " + std::string(e.what()));
        return nullptr;
    }
}