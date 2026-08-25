#pragma once
#include <string>
#include <vector>
#include "sensor_service.h"

struct NVRConfig {
    std::string nvrId;       
    std::string brand;       
    std::string ip;
    std::string username;
    std::string password;
    int port;        
};

// 摄像头配置（字段类型改为字符串）
struct CameraConfig {
    std::string cameraId;   
    std::string nvrId;       
    std::string name;
    int channelNo;           // 通道号仍为 int（代码中分配）
    std::string IPCIP;
    int port;
    std::string IPCUSER;
    std::string IPCPWD;
};

// ---------------- Serial ----------------
struct SerialConfig {
    std::string port;
    int baudRate = 0;
    std::string parity;
    int stopBits = 1;
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

// ---------------- GPIO Sensor (红外、水浸、烟感) ----------------
struct GPIOSensorConfig {
    std::string id;
    std::string name;
    std::string type;        // "infrared", "water_immersion", "smoke"
    int gpioPin = 0;
    std::string description;
};

// ---------------- 温湿度传感器配置 ----------------
// struct TempHumiditySensorConfig {
//     std::string id;
//     std::string name;
//     int modbusAddr = 1;
//     int tempRegister = 0;
//     int humidityRegister = 1;
//     int regCount = 2;
// };

struct TempHumiditySerialConfig {
    SerialConfig serial;
    std::vector<TempHumiditySensorConfig> sensors;
};

// ---------------- 门锁配置 ----------------
struct DoorLockConfig {
    std::string id;
    std::string name;
    std::string type;        // "gpio"
    int gpioPin = 0;
    std::string description;
};

// 全局配置
struct GlobalConfig {
    std::string version;
    std::string description;
    std::string boxId = "1";
    NVRConfig nvr;           // 单个 NVR 配置
    std::vector<CameraConfig> cameras;  // 摄像头列表
    std::vector<GPIOSensorConfig> gpioSensors;           // GPIO传感器（红外、水浸、烟感）
    TempHumiditySerialConfig tempHumiditySensor;         // 温湿度传感器配置
    std::vector<DoorLockConfig> doorLocks;               // 门锁配置
};

