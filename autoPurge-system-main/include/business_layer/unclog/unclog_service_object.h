#pragma once

#include "data_layer/unclog/unclog_data_layer_object.h"
#include "business_layer/device/device_service_object.h"
#include "common/config/config_object.h"

#include <optional>

enum class UnclogState {
    Scanning,       // Scanning test (巡检)
    Unclogging,     // Unclogging process (清堵)
    Stop_Unclogging, // 停止中清堵
    Maintenance,    // Operations and Maintenance (运维)
};

struct UnclogDeviceSet {
    std::optional<PlcDeviceConfig> pump;              // 通常 1 个（核心设备）

    std::vector<PlcDeviceConfig> valves;              // 多个电磁阀 / 三角阀
    std::vector<PlcDeviceConfig> swingMotors;         // 多个摆动机
    std::vector<SensorConfig> pressureSensors;        // 多个传感器（可能多个点位）
};


class UnclogDevices{
public:
    UnclogDevices() = default;
    ~UnclogDevices() = default;

    bool build(const RawDeviceInfo& raw);
    const std::optional<PlcDeviceConfig>& getPump();
    const std::vector<PlcDeviceConfig>& getValves();
    const std::vector<PlcDeviceConfig>& getSwingMotors();
    const std::vector<SensorConfig>& getPressureSensors();
private:
    std::optional<PlcDeviceConfig> findPump(const RawDeviceInfo& raw);
    std::vector<PlcDeviceConfig> findValves(const RawDeviceInfo& raw);
    std::vector<PlcDeviceConfig> findSwingMotors(const RawDeviceInfo& raw);    
    std::vector<SensorConfig> findPressureSensors(const RawDeviceInfo& raw);
    bool validate();
private:
    UnclogDeviceSet devices_;     
};



class SystemMode{


};


class UnclogStatus{


};

class  AlarmInfo{


};