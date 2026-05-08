#include "business_layer/unclog/unclog_service_object.h"



bool UnclogDevices::build(const RawDeviceInfo& raw){
    
}
const std::optional<PlcDeviceConfig>& UnclogDevices::getPump(){
    return devices_.pump;
}
const std::vector<PlcDeviceConfig>& UnclogDevices::getValves(){
    return devices_.valves;
}
const std::vector<PlcDeviceConfig>& UnclogDevices::getSwingMotors(){
     return devices_.swingMotors;
}
const std::vector<SensorConfig>& UnclogDevices::getPressureSensors(){
    return devices_.pressureSensors;
}

std::optional<PlcDeviceConfig> UnclogDevices::findPump(const RawDeviceInfo& raw){
    for (const auto& plc : raw.plcDevices) {
            if (plc.deviceType == "high_pressure_pump") {
                return plc; // 一般取第一个
            }
        }
    return std::nullopt;
}
std::vector<PlcDeviceConfig> UnclogDevices::findValves(const RawDeviceInfo& raw){
    std::vector<PlcDeviceConfig> result;

        for (const auto& plc : raw.plcDevices) {
            if (plc.deviceType == "solenoid_valve" ||
                plc.deviceType == "triangle_valve") {
                result.push_back(plc);
            }
        }
        return result;
}
std::vector<PlcDeviceConfig> UnclogDevices::findSwingMotors(const RawDeviceInfo& raw){
    std::vector<PlcDeviceConfig> result;

        for (const auto& plc : raw.plcDevices) {
            if (plc.deviceType == "swing_motor") {
                result.push_back(plc);
            }
        }
        return result;
}    
std::vector<SensorConfig> UnclogDevices::findPressureSensors(const RawDeviceInfo& raw){
     std::vector<SensorConfig> result;

        for (const auto& sensor : raw.sensors) {
            if (sensor.deviceType == "pressure") {
                result.push_back(sensor);
            }
        }
        return result;
}
bool UnclogDevices::validate(){
    if (!devices_.pump.has_value()) {
            return false; 
        }

    if (devices_.valves.empty()) {
            return false; // 至少要有一个阀
        }

    return true;
}