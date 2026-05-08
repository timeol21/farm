#pragma once
#include <string>
#include <unordered_map>
#include <optional>
struct RawSerialPort{
    int baud_rate ;
    int data_bits ;
    int stop_bits;
    std::string parity;
};

struct RawHostNode {
    std::string id;
    std::string name;
    std::string hostType;
    std::string ip;
};

struct RawInterface {
    std::string id;         // "/dev/ttyS4"
    std::string type;       // serial / gpio / ethernet
    std::string hostNodeId; // ⭐必须有
    RawSerialPort serialPort;
};

struct RawDevice {
    std::string id;
    std::string name;

    std::string deviceType;
    std::string vendor;
    std::string driverKey;

    std::string address;

    std::string hostNodeId;     // ⭐新增（强烈建议）
    std::string interfaceId;
    std::string parentDeviceId;

    std::string bindingMode;    // direct / gateway

    std::unordered_map<std::string, std::string> metadata;

    bool enabled = true;
};

enum class DeviceType {
    PLC,
    SENSOR,
    CAMERA,
    NVR,
    GPIO,
    RADAR,
    PLC_DEVICE,
    UNKNOWN
};

enum class DeviceRole {
    Sensor,
    Actuator,
    Gateway,//Platform
    Composite,
    LogicalPoint
};

enum class RuntimeBindingMode {
    Independent,   // 自己可直接通信
    Dependent      // 依附父设备访问
};

enum class InterfaceType {
    USB,
    Serial,
    RS485,
    Ethernet,
    GPIO,
    SDK,
    Virtual
};
// 中间网关（PLC/NVR/控制器）

struct InterfaceSerialPort{
    int baud_rate ;
    int data_bits ;
    int stop_bits;
    std::string parity;
};

struct HostNodeDefinition { //  宿主节点定义 （盒子/服务器/工控机）
    std::string id;
    std::string name;
    std::string hostType;    // edge_box / server / ipc
    std::string ip;
};

struct InterfaceDefinition { //接口总线（USB/485/以太网）
    std::string id;
    std::string hostNodeId;
    InterfaceType type;
    std::string endpoint;    // /dev/ttyUSB0 / eth0 / sdk0
    InterfaceSerialPort serialPort;
};

struct DeviceDefinition { // 设备定义
    std::string id;
    std::string name;


    DeviceType deviceTypeEnum;
    std::string deviceType;       // plc / temp_sensor / camera / nvr / robot_arm
    std::string vendor;         // hikvision / dahua / siemens / custom / dji

    DeviceRole role;

    std::string hostNodeId;       // 属于哪个宿主节点
    std::string interfaceId;      // 通过哪个接口接入
    std::string parentDeviceId;   // 如果依附在父设备上

    RuntimeBindingMode bindingMode;

    std::string driverKey;        // 创建驱动/运行时的关键字
    std::string address;          // IP / 通道 / 寄存器 / 点位地址 / rtsp_url

    std::unordered_map<std::string, std::string> metadata; //细节的字段
    bool enabled = true;
};

enum class VendorSdkType {
    Hikvision,
    RTSP,
    Dahua,
    RadarA,
    RadarB,
    PlcVendorA,
    PlcVendorB,
    Modbus,
    Unknown
};

inline std::string toString(VendorSdkType type) {
    switch (type) {
        case VendorSdkType::Hikvision: return "Hikvision";
        case VendorSdkType::RTSP: return "FFmpeg";
        case VendorSdkType::Dahua: return "Dahua";
        case VendorSdkType::RadarA: return "RadarA";
        case VendorSdkType::RadarB: return "RadarB";
        case VendorSdkType::PlcVendorA: return "PlcVendorA";
        case VendorSdkType::PlcVendorB: return "PlcVendorB";
        default: return "Unknown";
    }
}
inline std::string toString(DeviceRole role) {
    switch (role) {
        case DeviceRole::Sensor: return "Sensor";
        case DeviceRole::Actuator: return "Actuator";
        case DeviceRole::Gateway: return "Gateway";
        case DeviceRole::Composite: return "Composite";
        case DeviceRole::LogicalPoint: return "LogicalPoint";
    }
    return "Unknown";
};

inline std::string toString(RuntimeBindingMode mode) {
    switch (mode) {
        case RuntimeBindingMode::Independent: return "Independent";
        case RuntimeBindingMode::Dependent: return "Dependent";
    }
    return "Unknown";
};

inline std::string toString(InterfaceType type) {
    switch (type) {
        case InterfaceType::USB: return "USB";
        case InterfaceType::Serial: return "Serial";
        case InterfaceType::RS485: return "RS485";
        case InterfaceType::Ethernet: return "Ethernet";
        case InterfaceType::GPIO: return "GPIO";
        case InterfaceType::SDK: return "SDK";
        case InterfaceType::Virtual: return "Virtual";
    }
    return "Unknown";
};



struct CameraFrame{

};


enum class RuntimeDeviceStatus {
    OFFLINE,      // 离线：未连接、无响应
    ONLINE,       // 在线：已连接但未启动运行
    RUNNING,      // 运行：正常工作、数据采集/通信中
    EXCEPTION,    // 异常：连接正常但故障（报错、超时、码流丢失、通讯失败）
};




struct SensorData {
    // 核心属性
    float temperature;  // 温度
    float humidity;     // 湿度
    float pressure;     // 压力
    bool waterFlood; // 水浸（true=有水，false=无水）
    bool smoke;     // 烟感（true=告警，false=正常）}
    bool valid = false; //查看是否进行了操作
};


enum class SensorKind{
    TEMPERATURE_HUMIDITY,  // 温湿度
    WATER_IMMERSION,      // 水浸
    PRESSURE,             // 压力
    SMOKE_DETECTION       // 烟感
};



enum class PollingState {
    Normal,
    Backoff,
    Suspended   // 低频探活
};


struct PollingMeta {
    int failCount = 0;
    int currentInterval = 1;
    std::chrono::steady_clock::time_point nextPollTime;
};


class DeviceHealthStatus {
public:
    enum class Status {
        Normal,
        Exception,
        Timeout
    };

    DeviceHealthStatus() = default;

    DeviceHealthStatus(Status status, std::string msg = "")
        : status_(status), message_(std::move(msg)) {}

    Status getStatus() const ;
    const std::string& getMessage() const ;

    bool isNormal() const ;
    bool isTimeout() const ;
    bool isException() const ;

private:
    Status status_ = Status::Normal;
    std::string message_;
};

enum class DeviceOnlineStatus {
    Online,//在线
    Offline,//离线
    Unknown
};

struct  DeviceRuntimeStatus{
    std::string deviceId;

    DeviceOnlineStatus onlineStatus = DeviceOnlineStatus::Unknown;

    DeviceHealthStatus healthStatus;  // ✅ 加进来

    SensorData sensorData;

    int64_t lastUpdateTime = 0;       // 时间戳（非常重要）
};


struct PollingSnapshot {
    std::string deviceId;

    bool success;          // poll 是否成功（通信层）
    bool online;           // 是否在线（业务层）
    
    DeviceHealthStatus health; // 健康状态（Normal / Warning / Fault）

    std::chrono::steady_clock::time_point timestamp;

    std::string errorMsg;  // 失败原因（可选）

    // 可扩展数据（传感器数据 / 状态数据）
    std::optional<SensorData> sensorData;
};


