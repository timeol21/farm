#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/json.hpp"

#ifndef CONFIG_JSON
#define CONFIG_JSON	"/home/ztl/program/test/Farming-develop/include/config/config.json"
#endif

// 日志级别
enum class LogLevel {
	INFO,
	WARNING,
	ERROR
};

// 任务状态
enum class TaskState {
	NEW,        // 新建（任务创建但未分配资源）
    READY,      // 就绪（任务已加载到内存，等待分配CPU时间）
    RUNNING,    // 运行（任务正在执行）
    FINISHED,   // 完成
    FAILED      // 失败（执行发生异常）
};

// 任务控制块
struct TaskControlBlock {
	int id = 0;
	TaskState state = TaskState::NEW;

	std::chrono::steady_clock::time_point enqueueTime{};      // 进入队列的时间
    std::chrono::steady_clock::time_point startTime{};        // 开始执行的时间
    std::chrono::steady_clock::duration duration{};         // 已经执行的时间

	std::string name;       // 任务名称
    std::string deviceId;   // 目标设备
	std::string cmd;
	nlohmann::json params;
	std::string replyTo;
};

struct InternalTaskDefinition {
	std::string taskId;
	std::string name;
	std::string cmd;
	std::string deviceId;
	nlohmann::json params;
	int intervalMs = 0;
	bool enabled = true;
	std::chrono::steady_clock::time_point nextRunAt{};
	std::string source = "system";
	std::string replyTo;
	int consecutiveFailures = 0;
	int backoffLevel = 0;
	std::chrono::steady_clock::time_point lastSuccessAt{};
	std::chrono::steady_clock::time_point lastFailureAt{};
	std::string lastError;
};

// 请求
struct Request {
	std::string cmd;        // 指令
    std::string payload;    // 负载
    std::string source;     // 请求来源 (MQTT/HTTP)
    std::string replyTo;    // 回复地址 (MQTT/HTTP)
	
	Request() = default;
	Request(std::string cmd_, std::string payload_, std::string source_, std::string replyTo_)
		: cmd(std::move(cmd_)), payload(std::move(payload_)), source(std::move(source_)), replyTo(std::move(replyTo_)) {}
};

// 统一响应
template<typename T>
struct Result {
	int code = 200;
	std::string msg = "";
	T data;

	Result() = default;
	Result(int code_, std::string msg_, T data_): code(std::move(code_)), msg(std::move(msg_)), data(std::move(data_)) {}
};

// 序列化
template<typename T>
inline void to_json(nlohmann::json& j, const Result<T>& r) {
	j["code"] = r.code;
    j["msg"] = r.msg;
    j["data"] = r.data;
}

// 反序列化
template<typename T>
inline void from_json(const nlohmann::json& j, Result<T>& r) {
	j.at("code").get_to(r.code);
	j.at("msg").get_to(r.msg);
	j.at("data").get_to(r.data);
}

// 连接类型
enum class ConnectType {
	DIRECT,     // 直连
    PLC,        // PLC连接
    GATEWAY     // 网关连接
};

// 设备类型
enum class DeviceType {
	SENSOR,     // 传感器
    ACTUATOR,   // 执行器
	POSITION,	// 定位器
	CAMERA		// 摄像头
};

// ͨ通信协议类型
enum class ProtocolType {
	MODBUS,		// 此处应为MODBUS_RTP
	MQTT,
	TCPIP,
	HTTP,
	OTHER
};

// 设备状态码
enum class DeviceStatusCode {
	NORMAL = 0,     // 正常
    FAULT = 1,      // 故障
    OFFLINE = 2,    // 离线
    UNKNOWN = 3     // 未知状态
};

NLOHMANN_JSON_SERIALIZE_ENUM(DeviceType, {
	{DeviceType::SENSOR, "SENSOR"},
	{DeviceType::ACTUATOR, "ACTUATOR"},
	{DeviceType::POSITION, "POSITION"},
	{DeviceType::CAMERA, "CAMERA"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(ConnectType, {
	{ConnectType::DIRECT, "DIRECT"},
	{ConnectType::PLC, "PLC"},
	{ConnectType::GATEWAY, "GATEWAY"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolType, {
	{ProtocolType::MODBUS, "MODBUS"},
	{ProtocolType::MQTT, "MQTT"},
	{ProtocolType::TCPIP, "TCPIP"},
	{ProtocolType::HTTP, "HTTP"},
	{ProtocolType::OTHER, "OTHER"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(DeviceStatusCode, {
	{DeviceStatusCode::NORMAL, "NORMAL"},
	{DeviceStatusCode::FAULT, "FAULT"},
	{DeviceStatusCode::OFFLINE, "OFFLINE"},
	{DeviceStatusCode::UNKNOWN, "UNKNOWN"}
})

// 设备状态
struct DeviceState {
	std::string deviceId;

	DeviceType deviceType = DeviceType::SENSOR;
	ConnectType connectType = ConnectType::DIRECT;
	std::string channelId;      // 通信通道ID (PLC/Gateway ID)
    ProtocolType protocolType = ProtocolType::OTHER;  // 通信协议 (MODBUS等)

	bool online = false;
	DeviceStatusCode statusCode = DeviceStatusCode::NORMAL;
	// 设备状态数据，根据设备类型不同而不同
	std::unordered_map<std::string, std::string> ext;

	DeviceState() = default;

	DeviceState(
		std::string deviceId_,
		DeviceType deviceType_,
		ConnectType connectType_,
		std::string channelId_,
		ProtocolType protocolType_
	)
		: deviceId(std::move(deviceId_)),
		deviceType(deviceType_),
		connectType(connectType_),
		channelId(std::move(channelId_)),
		protocolType(protocolType_){}
};

inline void to_json(nlohmann::json& j, const DeviceState& s) {
    j["deviceId"]      = s.deviceId;
    j["deviceType"]    = s.deviceType;
    j["connectType"]   = s.connectType;
    j["channelId"]     = s.channelId;
    j["protocolType"]  = s.protocolType;
    j["online"]        = s.online;
    j["statusCode"]    = s.statusCode;
    j["ext"]           = s.ext;  // 你的扩展字段
}

// 设备组状态
struct DeviceGroupState {
	long long timestamp = 0;
	std::unordered_map<std::string, DeviceState> devices;	// 设备状态列表，key为deviceId
};

inline void to_json(nlohmann::json& j, const DeviceGroupState& g) {
    j["timestamp"] = g.timestamp;
    j["devices"] = g.devices;
}

struct ParseField {
    std::string key;
    int byte_offset = 0;
    int bytes = 0;
    bool signed_flag = false;
    std::string endian;
    float ratio = 1.0f;
    int offset = 0;
    std::string unit;
};

// 解析配置
struct ParserConfig {
    std::string type;
    std::string source;
    int min_length = 0;
    std::vector<ParseField> fields;
};

// MQTT订阅
struct MqttSubscription {
    std::string topic;
    int qos = 0;
};

// MQTT配置
struct MqttConfig {
    std::string broker;
    std::string client_id;
    std::vector<MqttSubscription> subscriptions;
	std::string status_report_topic;
};

// 系统配置
struct SystemConfig {
    std::vector<std::string> allowed_commands;
	bool auto_report_enabled = false;
	int auto_report_interval_ms = 60000;
};

struct ControlCommandConfig {
	std::string name;
	int register_address = 0;
	int value = 0;
	int function_code = 5;
};

struct FeedbackConfig {
	int register_address = 0;
	int function_code = 1;
};

struct ControlConfig {
	std::string kind;
	std::unordered_map<std::string, ControlCommandConfig> commands;
	FeedbackConfig feedback;
};

// 通道配置
struct ChannelConfig {
	std::string id;
	std::string type;
	std::string port;
	int baudrate = 9600;
	std::string parity = "N";
	int stopBits = 1;
	int dataBits = 8;
};

// 设备配置
struct DeviceConfig {
    std::string id;
    std::string type;
    std::string model;

    std::string protocol;
    std::string channelId;
    int interval = 6000;
    int modbus_slave_id = 1;
	ChannelConfig channelConfig;
    ParserConfig parser;
	ControlConfig control;
};

// 整个配置文件
struct AppConfig {
	std::vector<ChannelConfig> channels;
    std::vector<DeviceConfig> devices;
    MqttConfig mqtt;
    SystemConfig system;
};