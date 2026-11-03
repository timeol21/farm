#pragma once

#include "common/config/config_object.h"
#include <nlohmann/json.hpp>
#include <cmath>   // std::round(AiModelConfig 阈值写模板时规整精度)
#include <iostream> // 启动期解析告警走 stderr(那时 logger 还没注册)

namespace washcar {
namespace cfg {

using json = nlohmann::json;

// ========== 配置类型 ↔ JSON 互转(单一映射源) ==========
// 模板生成(default_config::toJson)与解析(ConfigManager::parseJson)都走这里,
// 加字段只改结构定义 + 对应 to_json/from_json 两处, 不再有第二份手写映射。
// 语义约定:
//   to_json   输出结构全部键(键名 = 运维可见的模板/配置文件键名, 改动即破坏兼容)。
//   from_json 只覆盖 JSON 里存在的键, 缺省键保留调用方现值(等价旧 j.value(key, 现值) 行为)。
//   数组型键(modelConfigs / regions / cameras): JSON 里存在该键才整体替换(空数组=清空),
//   不存在则保留现值。

// ---------- 摄像头 ----------
inline void to_json(json& j, const CameraConfig& c) {
    j = json{{"id", c.id}, {"name", c.name}, {"url", c.url}};
}
inline void from_json(const json& j, CameraConfig& c) {
    if (j.contains("id"))   j.at("id").get_to(c.id);
    if (j.contains("name")) j.at("name").get_to(c.name);
    if (j.contains("url"))  j.at("url").get_to(c.url);
}

// ---------- MQTT ----------
inline void to_json(json& j, const MqttConfig& m) {
    j = json{
        {"brokerIp",          m.brokerIp},
        {"port",              m.port},
        {"username",          m.username},
        {"password",          m.password},
        {"heartbeatSec",      m.heartbeatSec},
        {"reconnectBaseSec",  m.reconnectBaseSec},
        {"reconnectMaxRetry", m.reconnectMaxRetry},
        {"topicSeparator",    m.topicSeparator},
        {"rebootEnabled",     m.rebootEnabled}
    };
}
inline void from_json(const json& j, MqttConfig& m) {
    if (j.contains("brokerIp"))          j.at("brokerIp").get_to(m.brokerIp);
    if (j.contains("port"))              j.at("port").get_to(m.port);
    if (j.contains("username"))          j.at("username").get_to(m.username);
    if (j.contains("password"))          j.at("password").get_to(m.password);
    if (j.contains("heartbeatSec"))      j.at("heartbeatSec").get_to(m.heartbeatSec);
    if (j.contains("reconnectBaseSec"))  j.at("reconnectBaseSec").get_to(m.reconnectBaseSec);
    if (j.contains("reconnectMaxRetry")) j.at("reconnectMaxRetry").get_to(m.reconnectMaxRetry);
    if (j.contains("topicSeparator"))    j.at("topicSeparator").get_to(m.topicSeparator);
    if (j.contains("rebootEnabled"))     j.at("rebootEnabled").get_to(m.rebootEnabled);
}

// ---------- PLC 串口 ----------
inline void to_json(json& j, const PlcSerialConfig& p) {
    j = json{
        {"port",     p.port},
        {"baudRate", p.baudRate},
        {"parity",   p.parity},
        {"dataBits", p.dataBits},
        {"stopBits", p.stopBits}
    };
}
inline void from_json(const json& j, PlcSerialConfig& p) {
    if (j.contains("port"))         j.at("port").get_to(p.port);
    if (j.contains("baudRate"))     j.at("baudRate").get_to(p.baudRate);
    if (j.contains("parity"))       j.at("parity").get_to(p.parity);
    if (j.contains("dataBits"))     j.at("dataBits").get_to(p.dataBits);
    if (j.contains("stopBits"))     j.at("stopBits").get_to(p.stopBits);
    if (j.contains("timeoutMs"))    j.at("timeoutMs").get_to(p.timeoutMs);
    if (j.contains("frameDelayMs")) j.at("frameDelayMs").get_to(p.frameDelayMs);
}

// ---------- PLC Modbus TCP ----------
inline void to_json(json& j, const PlcTcpConfig& p) {
    j = json{
        {"plcId",  p.plcId},
        {"host",   p.host},
        {"port",   p.port},
        {"unitId", p.unitId}
    };
}
inline void from_json(const json& j, PlcTcpConfig& p) {
    if (j.contains("plcId"))     j.at("plcId").get_to(p.plcId);
    if (j.contains("host"))      j.at("host").get_to(p.host);
    if (j.contains("port"))      j.at("port").get_to(p.port);
    if (j.contains("unitId"))    j.at("unitId").get_to(p.unitId);
    if (j.contains("timeoutMs")) j.at("timeoutMs").get_to(p.timeoutMs);
}

// ---------- 系统 ----------
inline void to_json(json& j, const BoxConfig& b) {
    j = json{
        {"boxNo",      b.boxNo},
        {"dbPath",     b.dbPath},
        {"storageRoot", b.storageRoot},
        {"logLevel",   b.logLevel},
        {"logKeepDays", b.logKeepDays},
        {"plcEnabled", b.plcEnabled},
        {"boxType",    b.boxType}
    };
}
inline void from_json(const json& j, BoxConfig& b) {
    if (j.contains("boxNo"))      j.at("boxNo").get_to(b.boxNo);
    if (j.contains("dbPath"))     j.at("dbPath").get_to(b.dbPath);
    if (j.contains("storageRoot")) j.at("storageRoot").get_to(b.storageRoot);
    // logPath 已废弃: 日志目录固定为 <程序目录>/data/log。老配置里写了也不报错,直接忽略。
    if (j.contains("logLevel"))    j.at("logLevel").get_to(b.logLevel);
    if (j.contains("logKeepDays"))  j.at("logKeepDays").get_to(b.logKeepDays);
    if (j.contains("plcEnabled"))  j.at("plcEnabled").get_to(b.plcEnabled);
    if (j.contains("boxType"))    j.at("boxType").get_to(b.boxType);
}

// ---------- AI 模型 / 区域绑定 / AI ----------
inline void to_json(json& j, const AiRect& r) {
    j = json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
}
inline void from_json(const json& j, AiRect& r) {
    if (j.contains("x")) j.at("x").get_to(r.x);
    if (j.contains("y")) j.at("y").get_to(r.y);
    if (j.contains("w")) j.at("w").get_to(r.w);
    if (j.contains("h")) j.at("h").get_to(r.h);
}

inline void to_json(json& j, const AiModelConfig& m) {
    // float 阈值先 round 到 4 位小数再写: float 转 double 会带二进制尾巴
    // (如 0.45f -> 0.44999998807907104), 模板里保持 0.25/0.45 这种可读值
    const auto round4 = [](float v) {
        return static_cast<double>(std::round(v * 10000.0) / 10000.0);
    };
    j = json{
        {"modelId",       m.modelId},
        {"modelPaths",    m.modelPaths},
        {"classIds",      m.classIds},
        {"classTypes",    m.classTypes},
        {"boxThreshold",  round4(m.boxThreshold)},
        {"nmsThreshold",  round4(m.nmsThreshold)},
        {"modelKind",     m.modelKind},
        {"roi",           m.roi}
    };
}
inline void from_json(const json& j, AiModelConfig& m) {
    if (j.contains("modelId"))      j.at("modelId").get_to(m.modelId);
    if (j.contains("modelPath"))    j.at("modelPath").get_to(m.modelPath);
    if (j.contains("modelPaths"))   j.at("modelPaths").get_to(m.modelPaths);
    if (j.contains("classIds"))     j.at("classIds").get_to(m.classIds);
    if (j.contains("classTypes"))   j.at("classTypes").get_to(m.classTypes);
    if (j.contains("boxThreshold")) j.at("boxThreshold").get_to(m.boxThreshold);
    if (j.contains("nmsThreshold")) j.at("nmsThreshold").get_to(m.nmsThreshold);
    if (j.contains("modelKind"))    j.at("modelKind").get_to(m.modelKind);
    if (j.contains("roi"))          j.at("roi").get_to(m.roi);
    // 旧配置只写 modelPath(单个文件), 这里补成 modelPaths; 反向也补一份便于日志显示
    if (m.modelPaths.empty() && !m.modelPath.empty()) m.modelPaths.push_back(m.modelPath);
    if (m.modelPath.empty() && !m.modelPaths.empty()) m.modelPath = m.modelPaths[0];
}

inline void to_json(json& j, const AiRegionBinding& r) {
    j = json{
        {"regionId",        r.regionId},
        {"idleModelIds",    r.idleModelIds},
        {"workingModelIds", r.workingModelIds}
    };
}
inline void from_json(const json& j, AiRegionBinding& r) {
    if (j.contains("regionId"))        j.at("regionId").get_to(r.regionId);
    if (j.contains("idleModelIds"))    j.at("idleModelIds").get_to(r.idleModelIds);
    if (j.contains("workingModelIds")) j.at("workingModelIds").get_to(r.workingModelIds);
}

inline void to_json(json& j, const AiConfig& a) {
    j = json{
        {"aiEnabled",             a.aiEnabled},
        {"idleIntervalMs",        a.idleIntervalMs},
        {"workingIntervalMs",     a.workingIntervalMs},
        {"exitWorkingEmptyTimes", a.exitWorkingEmptyTimes},
        {"resultRetentionDays",   a.resultRetentionDays},
        {"maxDetectionResults",   a.maxDetectionResults},
        {"maxAlarmImageMb",       a.maxAlarmImageMb},
        {"alarmImageKeepDays",    a.alarmImageKeepDays},
        {"logSummaryIntervalSec", a.logSummaryIntervalSec},
        {"logSummaryMaxIntervalSec", a.logSummaryMaxIntervalSec},
        {"modelConfigs",          a.modelConfigs},
        {"regions",               a.regions}
    };
}
inline void from_json(const json& j, AiConfig& a) {
    if (j.contains("aiEnabled"))             j.at("aiEnabled").get_to(a.aiEnabled);
    if (j.contains("idleIntervalMs"))        j.at("idleIntervalMs").get_to(a.idleIntervalMs);
    if (j.contains("workingIntervalMs"))     j.at("workingIntervalMs").get_to(a.workingIntervalMs);
    if (j.contains("exitWorkingEmptyTimes")) j.at("exitWorkingEmptyTimes").get_to(a.exitWorkingEmptyTimes);
    if (j.contains("resultRetentionDays"))   j.at("resultRetentionDays").get_to(a.resultRetentionDays);
    if (j.contains("maxDetectionResults"))   j.at("maxDetectionResults").get_to(a.maxDetectionResults);
    if (j.contains("maxAlarmImageMb"))       j.at("maxAlarmImageMb").get_to(a.maxAlarmImageMb);
    if (j.contains("alarmImageKeepDays"))    j.at("alarmImageKeepDays").get_to(a.alarmImageKeepDays);
    if (j.contains("logSummaryIntervalSec")) j.at("logSummaryIntervalSec").get_to(a.logSummaryIntervalSec);
    if (j.contains("logSummaryMaxIntervalSec")) j.at("logSummaryMaxIntervalSec").get_to(a.logSummaryMaxIntervalSec);
    if (j.contains("modelConfigs")) {
        a.modelConfigs.clear();
        for (const auto& m : j["modelConfigs"]) {
            a.modelConfigs.push_back(m.get<AiModelConfig>());
        }
    }
    if (j.contains("regions")) {
        a.regions.clear();
        for (const auto& r : j["regions"]) {
            a.regions.push_back(r.get<AiRegionBinding>());
        }
    }
}

// ---------- HTTP ----------
inline void to_json(json& j, const HttpConfig& h) {
    j = json{{"bind", h.bind}, {"port", h.port}};
}
inline void from_json(const json& j, HttpConfig& h) {
    if (j.contains("bind")) j.at("bind").get_to(h.bind);
    if (j.contains("port")) j.at("port").get_to(h.port);
}

// ---------- 时间类 ----------
inline void to_json(json& j, const TimingConfig& t) {
    j = json{
        {"washcarMonitorIntervalMs",       t.washcarMonitorIntervalMs},
        {"washcarCommandConfirmTimeoutMs", t.washcarCommandConfirmTimeoutMs},
        {"dailyReportHour",          t.dailyReportHour},
        {"dailyReportMinute",        t.dailyReportMinute},
        {"dailyReportCheckIntervalMs", t.dailyReportCheckIntervalMs},
        {"faultReportIntervalMs",    t.faultReportIntervalMs},
        {"valueChangeIntervalMs",    t.valueChangeIntervalMs}
    };
}
inline void from_json(const json& j, TimingConfig& t) {
    if (j.contains("washcarMonitorIntervalMs"))       j.at("washcarMonitorIntervalMs").get_to(t.washcarMonitorIntervalMs);
    if (j.contains("washcarCommandConfirmTimeoutMs")) j.at("washcarCommandConfirmTimeoutMs").get_to(t.washcarCommandConfirmTimeoutMs);
    if (j.contains("dailyReportHour"))           j.at("dailyReportHour").get_to(t.dailyReportHour);
    if (j.contains("dailyReportMinute"))         j.at("dailyReportMinute").get_to(t.dailyReportMinute);
    if (j.contains("dailyReportCheckIntervalMs")) j.at("dailyReportCheckIntervalMs").get_to(t.dailyReportCheckIntervalMs);
    if (j.contains("faultReportIntervalMs"))     j.at("faultReportIntervalMs").get_to(t.faultReportIntervalMs);
    if (j.contains("valueChangeIntervalMs"))     j.at("valueChangeIntervalMs").get_to(t.valueChangeIntervalMs);
}

inline void to_json(json& j, const StateMachineConfig& s) {
    j = json{
        {"mode",              s.mode},
        {"logStateTransition", s.logStateTransition}
    };
}
inline void from_json(const json& j, StateMachineConfig& s) {
    if (j.contains("mode"))              j.at("mode").get_to(s.mode);
    if (j.contains("logStateTransition")) j.at("logStateTransition").get_to(s.logStateTransition);
}

// ---------- 洗车机点位表 ----------
inline void from_json(const json& j, AcquisitionConfig& a) {
    if (j.contains("intervalMs")) j.at("intervalMs").get_to(a.intervalMs);
    if (j.contains("D")) j.at("D").get_to(a.dPoints);
    if (j.contains("Y")) j.at("Y").get_to(a.yPoints);
    if (j.contains("M")) j.at("M").get_to(a.mPoints);
    if (j.contains("X")) j.at("X").get_to(a.xPoints);
    if (j.contains("S")) j.at("S").get_to(a.sPoints);
}

inline void from_json(const json& j, SignalConfig& s) {
    if (j.contains("name"))    j.at("name").get_to(s.name);
    if (j.contains("point"))   j.at("point").get_to(s.point);
    if (j.contains("report")) {
        const std::string r = j.at("report").get<std::string>();
        if (r == "property")      s.report = SignalReport::REPORT_PROPERTY;
        else if (r == "event")    s.report = SignalReport::REPORT_EVENT;
        else                      s.report = SignalReport::REPORT_NONE;
    }
    if (j.contains("cloudKey")) j.at("cloudKey").get_to(s.cloudKey);
    if (j.contains("normalValue")) j.at("normalValue").get_to(s.normalValue);
    if (j.contains("comment")) j.at("comment").get_to(s.comment);
}

inline void from_json(const json& j, ServiceConfig& s) {
    if (j.contains("name"))       j.at("name").get_to(s.name);
    if (j.contains("cloudField")) j.at("cloudField").get_to(s.cloudField);
    if (j.contains("write"))      j.at("write").get_to(s.write);
    if (j.contains("comment"))    j.at("comment").get_to(s.comment);
}

inline void from_json(const json& j, MachinePointTable& m) {
    if (j.contains("machineType"))   j.at("machineType").get_to(m.machineType);
    if (j.contains("protocol"))      j.at("protocol").get_to(m.protocol);
    if (j.contains("description"))   j.at("description").get_to(m.description);
    if (j.contains("acquisition"))   j.at("acquisition").get_to(m.acquisition);
    if (j.contains("signals")) {
        m.signals.clear();
        for (const auto& s : j["signals"]) m.signals.push_back(s.get<SignalConfig>());
    }
    if (j.contains("services")) {
        m.services.clear();
        for (const auto& s : j["services"]) m.services.push_back(s.get<ServiceConfig>());
    }
    if (j.contains("homeSensors")) j.at("homeSensors").get_to(m.homeSensors);
    if (j.contains("dailyReportSignals")) j.at("dailyReportSignals").get_to(m.dailyReportSignals);
    if (j.contains("faultSignals")) j.at("faultSignals").get_to(m.faultSignals);
    if (j.contains("resetSignals")) j.at("resetSignals").get_to(m.resetSignals);
    if (j.contains("faultCodes"))   j.at("faultCodes").get_to(m.faultCodes);
    if (j.contains("keyStatePoints")) j.at("keyStatePoints").get_to(m.keyStatePoints);
}

// ---------- 层间队列 ----------
inline void to_json(json& j, const QueueCapacityConfig& q) {
    j = json{
        {"downCapacity", q.downCapacity},
        {"upCapacity",   q.upCapacity},
        {"logCapacity",  q.logCapacity}
    };
}
inline void from_json(const json& j, QueueCapacityConfig& q) {
    if (j.contains("downCapacity")) j.at("downCapacity").get_to(q.downCapacity);
    if (j.contains("upCapacity"))   j.at("upCapacity").get_to(q.upCapacity);
    if (j.contains("logCapacity"))  j.at("logCapacity").get_to(q.logCapacity);
}

// ---------- 线程预算 ----------
inline void to_json(json& j, const ConcurrencyConfig& c) {
    j = json{{"maxThreads", c.maxThreads}};
}
inline void from_json(const json& j, ConcurrencyConfig& c) {
    if (j.contains("maxThreads")) j.at("maxThreads").get_to(c.maxThreads);
}

// ---------- 盒子运行状态监控 ----------
inline void to_json(json& j, const SysMonConfig& s) {
    j = json{
        {"enabled",    s.enabled},
        {"intervalMs", s.intervalMs}
    };
}
inline void from_json(const json& j, SysMonConfig& s) {
    if (j.contains("enabled"))    j.at("enabled").get_to(s.enabled);
    if (j.contains("intervalMs")) j.at("intervalMs").get_to(s.intervalMs);
}

// ---------- 根 ----------
inline void to_json(json& j, const DeviceConfigRoot& r) {
    j = json{
        {"box",       r.box},
        {"mqtt",      r.mqtt},
        {"plcSerial", r.plcSerial},
        {"http",      r.http},
        {"timing",    r.timing},
        {"queueCapacity", r.queueCapacity},
        {"sysmon",    r.sysmon},
        {"concurrency", r.concurrency},
        {"plcTcp",    r.plcTcp},
        {"ai",        r.ai},
        {"stateMachine", r.stateMachine},
        {"cameras",   r.cameras}
    };
}
inline void from_json(const json& j, DeviceConfigRoot& r) {
    if (j.contains("box"))       j.at("box").get_to(r.box);
    if (j.contains("mqtt"))      j.at("mqtt").get_to(r.mqtt);
    if (j.contains("plcSerial")) j.at("plcSerial").get_to(r.plcSerial);
    if (j.contains("http"))      j.at("http").get_to(r.http);
    if (j.contains("timing"))    j.at("timing").get_to(r.timing);
    // 队列容量段: 新名 queueCapacity; 老现场配置里的 queuing 仍然读,避免升级后静默回默认值
    if (j.contains("queueCapacity"))    j.at("queueCapacity").get_to(r.queueCapacity);
    else if (j.contains("queuing"))     j.at("queuing").get_to(r.queueCapacity);
    if (j.contains("sysmon"))    j.at("sysmon").get_to(r.sysmon);
    if (j.contains("concurrency")) j.at("concurrency").get_to(r.concurrency);
    if (j.contains("plcTcp"))    j.at("plcTcp").get_to(r.plcTcp);
    if (j.contains("ai"))        j.at("ai").get_to(r.ai);
    if (j.contains("stateMachine")) j.at("stateMachine").get_to(r.stateMachine);
    if (j.contains("machine")) {
        // "machine" 段按 boxType 分段:{"haiTian": {...}, "yiGongLi": {...}}
        // 某一项写坏了只跳过那一项,不让整份配置解析失败(跟"配置坏了也能起"一致);
        // 跳过的机型在 ConfigManager 里会回退到内置默认表。
        r.machines.clear();
        for (const auto& item : j.at("machine").items()) {
            try {
                r.machines[item.key()] = item.value().get<MachinePointTable>();
            } catch (const std::exception& e) {
                std::cerr << "[Config] machine." << item.key() << " 解析失败,这一项用内置默认表: "
                          << e.what() << std::endl;
            }
        }
    }
    if (j.contains("cameras")) {
        r.cameras.clear();
        for (const auto& c : j["cameras"]) {
            r.cameras.push_back(c.get<CameraConfig>());
        }
    }
}

}  // namespace cfg
}  // namespace washcar
