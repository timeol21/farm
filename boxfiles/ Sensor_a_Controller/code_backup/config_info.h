#pragma once
#include <string>
#include <vector>

// ---------------- Camera ----------------
struct CameraConfig {
    std::string id;
    std::string name;
    std::string url;
};

// ---------------- Serial ----------------
struct SerialConfig {
    std::string port;
    int baudRate = 0;
    std::string parity;
    int stopBits = 1;
};

// ---------------- PLC 本体：直连配置 ----------------
struct PLCDirectConfig {
    SerialConfig serial;
};

// ---------------- PLC 本体：网关配置 ----------------
struct PLCGatewayBase {
    std::string gatewayId;
    std::string gatewayIp;
    int gatewayPort = 0;
};

// ---------------- PLC 本体结构 ----------------
struct PLCConfig {
    std::string plcId;
    std::string name;
    uint8_t slaveId = 1;
    std::string connectionType; // direct / gateway

    bool hasSerial = false;
    PLCDirectConfig serialConfig;

    bool hasGateway = false;
    PLCGatewayBase gatewayConfig;
};


// ---------------- 下挂设备 ----------------
struct PLCDeviceConfig {
    std::string id;
    std::string plcId;   // 归属 PLC！！
    std::string name;
    std::string deviceType; // 设备类型，例如 "solenoid_valve"
    std::string registerAddress;   // "0x0001"
};


// ---------------- Sensor ----------------
struct SensorConfig {
    std::string id;
    std::string name;
    std::string type;
    SerialConfig serial;
    
    int modbusAddr = 1;
    int regStart = 0;
    int regCount = 2;
};

// ---------------- Gateway (业务网关信息) ----------------
struct GatewayConfig {
    std::string id;
    std::string name;
    std::string ip;
    std::string protocol;
    std::string status;
};

// ---------------- CarControl ----------------
struct CarControlConfig {
    std::string id;
    std::string name;
    SerialConfig serial;
    // timing parameters (ms)
    int sendWindowMs = 600;       // total continuous send window
    int sendIntervalMs = 80;      // interval between sends
    int operateTimeoutMs = 1500;  // timeout for operate() waiting
};

// ---------------- Root ----------------
struct DeviceConfigRoot {
    std::string version;
    std::string description;
    std::string boxId = "1";

    std::vector<CameraConfig> cameras;
    std::vector<PLCConfig> plcs;            // <-- 新增
    std::vector<PLCDeviceConfig> plcDevices;
    std::vector<SensorConfig> sensors;
    std::vector<GatewayConfig> gateways;
    std::vector<CarControlConfig> carControls;
};
