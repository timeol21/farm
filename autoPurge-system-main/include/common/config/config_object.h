#pragma once
#include <string>
#include "common/config/json_util.h"

// ===================== 串口配置 =====================
struct SerialPortConfig {
    int baud_rate ;
    int data_bits ;
    int stop_bits;
    std::string parity ;
    int read_timeout_ms ;
    int max_retries;
};

// ===================== 主机节点（edge_box） =====================
struct HostNode {
    std::string id;
    std::string name;
    std::string hostType;
    std::string ip;
};

// ===================== 接口配置（serial/gpio/ethernet） =====================
struct InterfaceConfig {
    std::string id;
    std::string type;
    std::string hostNodeId;
    SerialPortConfig serialConfig; // 仅串口使用
};

// ===================== 设备元数据（匹配JSON） =====================
struct DeviceMetadata {
    // NVR/RTSP
    std::string username;
    std::string password;
    std::string port;
    std::string rtsp_url;
    
    // 摄像头
    std::string channel;

    // PLC
    std::string slave_addr;
    std::string plc_port;

    // 传感器
    std::string reg_start;
    std::string reg_count;

    // GPIO设备
    std::string direction;
    std::string active_logic;

    // 你解析里用到的额外字段（必须加上，否则编译报错）
    std::string type;
    std::string logic;
    std::map<std::string, std::vector<std::string>> commands;
};

// ===================== 各类设备结构体（完全匹配JSON） =====================
struct NvrConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string vendor;
    std::string hostNodeId;
    std::string driverKey;
    std::string address;
    DeviceMetadata metadata;
};

struct CameraConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string vendor;
    std::string parentDeviceId;
    std::string hostNodeId;
    std::string driverKey;
    std::string address;
    DeviceMetadata metadata;
};

struct PlcConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string vendor;
    std::string hostNodeId;
    std::string interfaceId;
    std::string driverKey;
    std::string bindingMode;
    std::string address;
    DeviceMetadata metadata;
};

struct PlcDeviceConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string parentDeviceId;
    std::string interfaceId;
    std::string driverKey;
    std::string address;
    DeviceMetadata metadata;
};

struct SensorConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string driverKey;
    std::string interfaceId;
    std::string address;
    DeviceMetadata metadata;
};

struct GpioDeviceConfig {
    std::string id;
    std::string name;
    std::string deviceType;
    std::string driverKey;
    std::string interfaceId;
    std::string address;
    DeviceMetadata metadata;
};

// ===================== 硬件配置 ======================
struct Hardware {
    std::string gpio_chip;
};

// ===================== 设备配置主类 ======================
class DeviceConfig {
public:
    DeviceConfig() = default;
    ~DeviceConfig() = default;

    void updateDeviceConfigPath(const std::string& path);
    bool load();

    const std::vector<HostNode>& getHostNode() const;
    const std::map<std::string, InterfaceConfig>& getInterfaces() const;
    const NvrConfig& getNvr() const;
    const std::vector<CameraConfig>& getCameras() const;
    const std::vector<PlcConfig>& getPlcs() const;
    const std::vector<PlcDeviceConfig>& getPlcDevices() const;
    const std::vector<SensorConfig>& getSensors() const;
    const std::vector<GpioDeviceConfig>& getGpioDevices() const;

private:
    void parse(const json& j);
    void parseHostNodes(const json& j);
    void parseInterfaces(const json& j);
    void parseDevices(const json& j);
    DeviceMetadata parseMetadata(const json& j);
    SerialPortConfig parseSerialConfig(const json& j);

    NvrConfig parseNvr(const json& j);
    CameraConfig parseCamera(const json& j);
    PlcConfig parsePlc(const json& j);
    PlcDeviceConfig parsePlcDevice(const json& j);
    SensorConfig parseSensor(const json& j);
    GpioDeviceConfig parseGpioDevice(const json& j);

private:
    std::string config_path_;
    std::string version_;
    std::string description_;

    std::vector<HostNode> host_node_;
    std::map<std::string, InterfaceConfig> interfaces_;

    NvrConfig nvr_;
    std::vector<CameraConfig> cameras_;
    std::vector<PlcConfig> plcs_;
    std::vector<PlcDeviceConfig> plc_devices_;
    std::vector<SensorConfig> sensors_;
    std::vector<GpioDeviceConfig> gpio_devices_;
};

// ===================== 服务配置 ======================
struct UnclogServiceConfig {
    bool enabled = false;
    int check_interval_ms = 0;
    bool auto_trigger = false;
    int timeout_ms = 0;
};

struct AIServiceConfig {
    bool enabled = false;
    std::string model_path;
    float confidence_threshold = 0.0f;
    float nms_threshold = 0.0f;
};

struct DetectionServiceConfig {
    AIServiceConfig ai;
};

struct WebServiceConfig {
    bool enabled = false;
    std::string ip;
    int port = 0;
    int thread_pool_size = 0;
};

struct MqttServiceConfig {
    bool enabled = false;
    std::string broker_ip;
    int port = 0;
    std::string client_id;
    int qos = 0;
};

struct DatabaseConfig {
    bool enabled = false;
    std::string type;
    std::string path;
};

struct DeviceServiceConfig {
    bool enabled = false;
    int polling_interval_ms = 0;
    Hardware hardware;
    DeviceConfig config;
};

struct Services {
    UnclogServiceConfig unclog_service;
    DeviceServiceConfig device_service;
    DetectionServiceConfig detection_service;
    WebServiceConfig web_service;
    MqttServiceConfig mqtt_service;
    DatabaseConfig database;
};

// ===================== 系统信息 ======================
struct SystemInfo {
    std::string name;
    std::string mode;
};

// ===================== 日志 ======================
struct Logging {
    std::string level;
    std::string path;
    int max_size_mb = 0;
    int max_files = 0;
};

