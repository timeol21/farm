#include "common/config/config_manager.h"
#include "common/config/config_serialization.h"
#include "common/config/default_point.h"
#include "common/config/point.h"
#include "default_config.cpp"               // ← 直接 include 拿到 default_config::get() / toJson()
#include "common/log/log_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <set>
#include <unordered_set>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <sys/stat.h>

using json = nlohmann::json;
using namespace washcar::cfg;

// 配置加载发生在日志系统起来之前(日志目录本身来自配置),
// 这时候 LOG_* 会被 LoggerManager 丢掉("work" logger 还没注册),
// 所以启动期这几条消息走 stderr,现场在控制台/journal 里能直接看到。
static void startupLog(const std::string& msg) {
    std::cerr << "[Config] " << msg << std::endl;
}

namespace {

// 配置版本号:FNV-1a 64 位。只用于"文件有没有被别人改过"的冲突检测,
// 不需要密码学强度,所以不引 OpenSSL,避免测试 target 多链一个库。
std::string revisionOf(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

std::string nowText() {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&now, &tmBuf);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    return std::string(buf);
}

std::string timestampId() {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&now, &tmBuf);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                  tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    return std::string(buf);
}

bool readWholeFile(const std::string& path, std::string& text, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

bool writeAtomic(const std::string& path, const std::string& text, std::string& error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "cannot write " + tmp;
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good()) {
            error = "write failed " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "rename failed: " + ec.message();
        return false;
    }
    ::chmod(path.c_str(), 0600);
    return true;
}

std::string parentDir(const std::string& path) {
    std::filesystem::path p(path);
    return p.parent_path().empty() ? std::string(".") : p.parent_path().string();
}

std::string lastGoodPath(const std::string& path) {
    return (std::filesystem::path(parentDir(path)) / "config.lastgood.json").string();
}

std::string historyDir(const std::string& path) {
    return (std::filesystem::path(parentDir(path)) / "config_history").string();
}

std::string metaPath(const std::string& path) {
    return (std::filesystem::path(parentDir(path)) / "config.meta.json").string();
}

void deepMerge(json& dst, const json& patch) {
    if (!dst.is_object() || !patch.is_object()) {
        dst = patch;
        return;
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (dst.contains(it.key()) && dst[it.key()].is_object() && it.value().is_object()) {
            deepMerge(dst[it.key()], it.value());
        } else {
            dst[it.key()] = it.value();
        }
    }
}

bool isValidTopicChar(unsigned char c) {
    return c >= 0x20 && c != 0x7f && c != '#' && c != '+' && c != '$';
}

bool validateTopics(const washcar::cfg::MqttTopicsConfig& topics, const std::string& boxNo,
                    std::string& error) {
    if (boxNo.empty()) {
        error = "box.boxNo 不能为空";
        return false;
    }
    if (boxNo.find('/') != std::string::npos || boxNo.find('.') != std::string::npos ||
        boxNo.find(' ') != std::string::npos || boxNo.find('#') != std::string::npos ||
        boxNo.find('+') != std::string::npos) {
        error = "box.boxNo 不能包含 / . 空格 # +";
        return false;
    }
    const std::vector<std::pair<std::string, std::string>> all = {
        {"monitor", topics.monitor}, {"readdata", topics.readdata},
        {"read", topics.read}, {"pushalarm", topics.pushalarm},
        {"command", topics.command}, {"commandreply", topics.commandreply},
        {"reboot", topics.reboot}
    };
    std::set<std::string> seen;
    for (const auto& [key, raw] : all) {
        if (raw.empty()) {
            error = "MQTT 主题 " + key + " 不能为空";
            return false;
        }
        if (raw.size() > 256) {
            error = "MQTT 主题 " + key + " 超过 256 字符";
            return false;
        }
        if (raw.find("{boxNo}") == std::string::npos && raw.find(boxNo) == std::string::npos) {
            error = "MQTT 主题 " + key + " 必须包含 {boxNo} 或当前盒号";
            return false;
        }
        for (unsigned char c : raw) {
            if (!isValidTopicChar(c)) {
                error = "MQTT 主题 " + key + " 含非法字符(空格/#/+/$ 或控制字符)";
                return false;
            }
        }
        std::string resolved = raw;
        const std::string placeholder = "{boxNo}";
        std::size_t pos = 0;
        while ((pos = resolved.find(placeholder, pos)) != std::string::npos) {
            resolved.replace(pos, placeholder.size(), boxNo);
            pos += boxNo.size();
        }
        if (!seen.insert(resolved).second) {
            error = "MQTT 主题重复: " + resolved;
            return false;
        }
    }
    return true;
}

void addPointToAcquisition(washcar::cfg::MachinePointTable& table, const std::string& point) {
    if (point.empty()) return;
    const auto parsed = washcar::cfg::parsePoint(point);
    if (parsed.type == '?') return;
    std::vector<std::string>* list = nullptr;
    switch (parsed.type) {
    case 'D': list = &table.acquisition.dPoints; break;
    case 'Y': list = &table.acquisition.yPoints; break;
    case 'M': list = &table.acquisition.mPoints; break;
    case 'X': list = &table.acquisition.xPoints; break;
    case 'S': list = &table.acquisition.sPoints; break;
    default: return;
    }
    if (std::find(list->begin(), list->end(), point) == list->end()) list->push_back(point);
}

void appendUnique(std::vector<std::string>& dst, const std::vector<std::string>& src) {
    for (const auto& item : src) {
        if (std::find(dst.begin(), dst.end(), item) == dst.end()) dst.push_back(item);
    }
}

washcar::cfg::MachinePointTable mergePointTable(const washcar::cfg::MachinePointTable& base,
                                                 const washcar::cfg::MachinePointTable& delta) {
    washcar::cfg::MachinePointTable merged = base;
    std::set<std::string> baseNames;
    for (const auto& signal : base.signals) baseNames.insert(signal.name);

    // 只接受"新增"信号;内置点位禁止通过配置覆盖(点位/通道/云端字段都以代码为准)。
    for (const auto& signal : delta.signals) {
        if (baseNames.count(signal.name)) {
            startupLog("[Config] 忽略对内置点位 " + signal.name + " 的覆盖;内置点位只读");
            continue;
        }
        merged.signals.push_back(signal);
        addPointToAcquisition(merged, signal.point);
    }

    // 采集清单取并集:允许手工额外采点(无信号绑定),但不会删掉内置点。
    appendUnique(merged.acquisition.dPoints, delta.acquisition.dPoints);
    appendUnique(merged.acquisition.yPoints, delta.acquisition.yPoints);
    appendUnique(merged.acquisition.mPoints, delta.acquisition.mPoints);
    appendUnique(merged.acquisition.xPoints, delta.acquisition.xPoints);
    appendUnique(merged.acquisition.sPoints, delta.acquisition.sPoints);

    appendUnique(merged.faultSignals, delta.faultSignals);
    appendUnique(merged.resetSignals, delta.resetSignals);
    appendUnique(merged.homeSensors, delta.homeSensors);
    appendUnique(merged.dailyReportSignals, delta.dailyReportSignals);
    appendUnique(merged.keyStatePoints, delta.keyStatePoints);
    for (const auto& [code, label] : delta.faultCodes) merged.faultCodes[code] = label;

    // 启动准入规则:按"规则键"覆盖内置规则(signal 名 / activeStep),多出来的追加。
    // 这样现场只写要改的那一条,不用整段抄;也能用 enabled=false 关掉内置规则。
    {
        const auto ruleKey = [](const washcar::cfg::StartCheckConfig& rule) {
            return rule.kind == "activeStep" ? std::string("activeStep")
                                             : std::string("signal:") + rule.signal;
        };
        for (const auto& rule : delta.startChecks) {
            const std::string key = ruleKey(rule);
            auto it = std::find_if(merged.startChecks.begin(), merged.startChecks.end(),
                                   [&](const washcar::cfg::StartCheckConfig& existing) {
                                       return ruleKey(existing) == key;
                                   });
            if (it != merged.startChecks.end()) {
                // 局部覆盖: 配置没写的字段沿用内置规则(只改 missing / 只停用都不用整条抄)
                washcar::cfg::StartCheckConfig updated = rule;
                if (updated.reason.empty()) updated.reason = it->reason;
                if (updated.suggestion.empty()) updated.suggestion = it->suggestion;
                if (updated.reasonIf.empty()) updated.reasonIf = it->reasonIf;
                if (updated.kind.empty()) updated.kind = it->kind;
                if (!updated.missingSpecified) {
                    updated.missing = it->missing;
                    updated.missingSpecified = it->missingSpecified;
                }
                *it = updated;
            } else {
                merged.startChecks.push_back(rule);
            }
        }
    }

    // 命令写点属于安全边界,配置台 V1 不允许改;只接受新增服务名,不覆盖内置服务。
    std::set<std::string> serviceNames;
    for (const auto& service : base.services) serviceNames.insert(service.name);
    for (const auto& service : delta.services) {
        if (serviceNames.count(service.name)) continue;
        merged.services.push_back(service);
    }
    return merged;
}

bool parseDocument(const json& raw, washcar::cfg::DeviceConfigRoot& root,
                   washcar::cfg::MachinePointTable& active, std::string& error,
                   bool strictMachine) {
    try {
        root = default_config::get();
        from_json(raw, root);
    } catch (const std::exception& e) {
        error = std::string("配置字段类型错误: ") + e.what();
        return false;
    }

    if (!raw.contains("box") || !raw["box"].is_object() || !raw["box"].contains("boxNo") ||
        root.box.boxNo.empty()) {
        error = "box.boxNo 缺失或为空";
        return false;
    }
    if (!raw.contains("mqtt") || !raw["mqtt"].is_object() || !raw["mqtt"].contains("brokerIp") ||
        root.mqtt.brokerIp.empty()) {
        error = "mqtt.brokerIp 缺失或为空";
        return false;
    }
    if (!validateTopics(root.mqtt.topics, root.box.boxNo, error)) return false;

    const auto base = default_point::getPointTable(root.box.boxType);
    washcar::cfg::MachinePointTable delta;
    bool hasDelta = false;
    if (raw.contains("machine") && raw["machine"].is_object() &&
        raw["machine"].contains(root.box.boxType)) {
        try {
            delta = raw["machine"][root.box.boxType].get<washcar::cfg::MachinePointTable>();
            hasDelta = true;
        } catch (const std::exception& e) {
            error = std::string("machine.") + root.box.boxType + " 解析失败: " + e.what();
            return false;
        }
    }

    auto merged = hasDelta ? mergePointTable(base, delta) : base;
    std::string pointError;
    if (!washcar::cfg::validatePointTable(merged, pointError)) {
        if (strictMachine) {
            error = pointError;
            return false;
        }
        startupLog("[Config] machine 段校验不通过,回退内置表: " + pointError);
        merged = base;
    }
    active = merged;
    return true;
}

bool isValidPointFormat(const std::string& point) {
    if (point.size() < 2) return false;
    const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(point[0])));
    if (type != 'D' && type != 'Y' && type != 'M' && type != 'X' && type != 'S') return false;
    for (size_t index = 1; index < point.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(point[index]))) return false;
    }
    return true;
}

}  // namespace

namespace washcar {
namespace cfg {





// 校验一份洗车机点位表自洽。返回 false 时 errorOut 里写清楚哪一条不通过。
// 内容: 点位格式合法 / X-Y 位号在 0~7 / name 唯一 / 引用存在 / 采集读次数不超预算。
bool validatePointTable(const MachinePointTable& pointTable, std::string& errorOut) {
    std::ostringstream errors;

    // 1) 采集点位:格式合法 + X/Y 位号 0~7 + 收进已采集合
    std::set<std::string> acquiredPoints;
    const std::vector<const std::vector<std::string>*> pointGroups = {
        &pointTable.acquisition.dPoints, &pointTable.acquisition.yPoints,
        &pointTable.acquisition.mPoints, &pointTable.acquisition.xPoints,
        &pointTable.acquisition.sPoints
    };
    for (const auto* group : pointGroups) {
        for (const auto& point : *group) {
            if (!isValidPointFormat(point)) {
                errors << "采集点位格式非法: '" << point << "'(应为 D/Y/M/X/S + 数字)\n";
                continue;
            }
            const auto parsed = parsePoint(point);
            if ((parsed.type == 'X' || parsed.type == 'Y') && (parsed.addr % 10) > 7) {
                errors << "采集点位 " << point << " 的位号超过 7(FX 组读只能读 0~7)\n";
            }
            acquiredPoints.insert(point);
        }
    }

    if (pointTable.protocol == "modbusTcp" &&
        (!pointTable.acquisition.xPoints.empty() || !pointTable.acquisition.yPoints.empty())) {
        errors << "modbusTcp 机型不支持 X/Y 采集点,请从 acquisition 里去掉\n";
    }

    // 2) 信号: name 唯一 + 点位格式合法 + 点位在采集清单里 + 云端字段不撞车
    std::set<std::string> signalNames;
    std::map<std::string, std::set<std::string>> cloudKeys;
    for (const auto& signal : pointTable.signals) {
        if (signal.name.empty()) {
            errors << "信号缺 name\n";
            continue;
        }
        if (!signalNames.insert(signal.name).second) {
            errors << "信号 name 重复: '" << signal.name << "'\n";
        }
        if (!isValidPointFormat(signal.point)) {
            errors << "信号 '" << signal.name << "' 的点位格式非法: '" << signal.point << "'\n";
        } else if (!acquiredPoints.count(signal.point)) {
            errors << "信号 '" << signal.name << "' 的点位 " << signal.point << " 不在采集清单里\n";
        }

        std::string key = signal.cloudKey;
        if (key.empty()) {
            key = signal.point;
            for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        const auto addKey = [&](const char* channel, bool enabled) {
            if (!enabled) return;
            if (!cloudKeys[channel].insert(key).second) {
                errors << "上报通道 " << channel << " 的云端字段重复: '" << key
                       << "'(点位 " << signal.point << ")\n";
            }
        };
        addKey("monitor", hasReport(signal.report, SignalReport::REPORT_MONITOR));
        addKey("readdata", hasReport(signal.report, SignalReport::REPORT_READDATA));
        addKey("pushalarm", hasReport(signal.report, SignalReport::REPORT_PUSHALARM));
    }

    // 3) 引用的 name 必须存在
    const auto checkRefs = [&](const std::vector<std::string>& refs, const char* what) {
        for (const auto& ref : refs) {
            if (!signalNames.count(ref)) {
                errors << what << " 引用了不存在的信号名: '" << ref << "'\n";
            }
        }
    };
    checkRefs(pointTable.faultSignals, "faultSignals");
    checkRefs(pointTable.resetSignals, "resetSignals");
    checkRefs(pointTable.homeSensors, "homeSensors");
    checkRefs(pointTable.keyStatePoints, "keyStatePoints");
    checkRefs(pointTable.dailyReportSignals, "dailyReportSignals");

    // 3.5) 启动准入规则: 有内容、kind/missing 合法、引用的信号存在、文案不空
    {
        if (pointTable.startChecks.empty()) {
            errors << "startChecks 为空: 启动准入必须至少有一条规则\n";
        }
        if (pointTable.startChecks.size() > 20) {
            errors << "startChecks 规则过多: " << pointTable.startChecks.size() << " 条(上限 20)\n";
        }
        for (std::size_t i = 0; i < pointTable.startChecks.size(); ++i) {
            const auto& rule = pointTable.startChecks[i];
            if (!rule.enabled) continue;   // 停用的规则不参与判定, 也不要求文案
            const std::string where = "startChecks[" + std::to_string(i) + "]";
            if (rule.kind != "signal" && rule.kind != "activeStep") {
                errors << where << " kind 非法: '" << rule.kind << "'(应为 signal / activeStep)\n";
            }
            if (rule.kind == "signal") {
                if (rule.signal.empty()) {
                    errors << where << " 缺 signal\n";
                } else if (!signalNames.count(rule.signal)) {
                    errors << where << " 引用了不存在的信号: '" << rule.signal << "'\n";
                }
            }
            if (!rule.missingSpecified) {
                errors << where << " 缺 missing(必须显式写 allow / reject)\n";
            } else if (rule.missing != "allow" && rule.missing != "reject") {
                errors << where << " missing 非法: '" << rule.missing << "'(应为 allow / reject)\n";
            }
            if (rule.reason.empty()) errors << where << " 缺 reason\n";
            for (const auto& alt : rule.reasonIf) {
                if (!alt.signal.empty() && !signalNames.count(alt.signal)) {
                    errors << where << " reasonIf 引用了不存在的信号: '" << alt.signal << "'\n";
                }
                if (alt.reason.empty()) errors << where << " reasonIf 缺 reason\n";
            }
        }
    }

    // 4) 命令写点位格式(写点不要求进采集清单,很多是只写不读的)
    for (const auto& service : pointTable.services) {
        for (const auto& [selector, point] : service.write) {
            (void)selector;
            if (!isValidPointFormat(point)) {
                errors << "服务 '" << service.name << "' 的写点位格式非法: '" << point << "'\n";
            }
        }
    }

    // 5) 采集预算:串口/Modbus 单点读,一轮太多会把采集周期拖长
    std::set<int> xGroups, yGroups;
    for (const auto& p : pointTable.acquisition.xPoints) xGroups.insert(parsePoint(p).addr / 10);
    for (const auto& p : pointTable.acquisition.yPoints) yGroups.insert(parsePoint(p).addr / 10);
    const std::size_t reads = pointTable.acquisition.dPoints.size()
                            + pointTable.acquisition.mPoints.size()
                            + pointTable.acquisition.sPoints.size()
                            + xGroups.size() + yGroups.size();
    if (reads > 300) {
        errors << "采集读次数过多: " << reads << " 次/周期(上限 300),请减少监控点或加大采集周期\n";
    }

    if (errors.str().empty()) return true;
    errorOut = errors.str();
    return false;
}

}  // namespace cfg
}  // namespace washcar

ConfigManager& ConfigManager::instance() {
    static ConfigManager mgr;
    return mgr;
}

nlohmann::json ConfigManager::raw() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_raw;
}

std::string ConfigManager::revision() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_revision;
}

void ConfigManager::applyDefaults() {
    root_ = default_config::get();
    activeMachine_ = default_point::getPointTable(root_.box.boxType);
}

bool ConfigManager::load(const std::string& path, std::string& errorOut) {
    m_path = path;
    m_degraded = false;

    std::error_code ec;
    std::filesystem::create_directories(parentDir(path), ec);

    std::string text;
    std::string readError;
    const bool exists = readWholeFile(path, text, readError);
    if (!exists || text.empty()) {
        if (!writeTemplate(path, errorOut)) return false;
        if (!readWholeFile(path, text, errorOut)) return false;
        startupLog("[Config] generated template at " + path);
    }

    std::string firstError;
    if (!parseJson(text, firstError)) {
        // 手改坏一个字符不应该让盒子起不来:优先回退上一次成功启动时的配置。
        std::string goodText;
        std::string goodError;
        if (readWholeFile(lastGoodPath(path), goodText, goodError) &&
            parseJson(goodText, goodError)) {
            m_degraded = true;
            startupLog("[Config] config.json 解析失败,已回退 config.lastgood.json: " + firstError);
            return true;
        }
        startupLog("[Config] parse failed: " + firstError + ", fallback to defaults");
        applyDefaults();
        m_raw = json::object();
        m_rawText.clear();
        m_revision = revisionOf("");
        errorOut = firstError;
        return false;
    }

    resolveEnvVars(root_.mqtt.password);
    ::chmod(path.c_str(), 0600);

    if (root_.box.boxNo.empty()) {
        errorOut = "box.boxNo required";
        startupLog(errorOut);
        return false;
    }
    if (root_.mqtt.brokerIp.empty()) {
        errorOut = "mqtt.brokerIp required";
        startupLog(errorOut);
        return false;
    }

    const std::size_t baseSignals = default_point::getPointTable(root_.box.boxType).signals.size();
    const std::size_t customSignals = activeMachine_.signals.size() > baseSignals
        ? activeMachine_.signals.size() - baseSignals : 0;
    startupLog("[Config] loaded from " + path
             + ", cameras=" + std::to_string(root_.cameras.size())
             + ", machineType=" + activeMachine_.machineType
             + ", customSignals=" + std::to_string(customSignals));
    return true;
}

bool ConfigManager::writeTemplate(const std::string& path, std::string& errorOut) {
    try {
        return writeAtomic(path, default_config::toJson(), errorOut);
    } catch (const std::exception& e) {
        errorOut = std::string("toJson threw: ") + e.what();
        return false;
    }
}

const MachinePointTable& ConfigManager::machine(const std::string& boxType) const {
    if (boxType == root_.box.boxType) return activeMachine_;
    return default_point::getPointTable(boxType);
}

bool ConfigManager::parseJson(const std::string& text, std::string& errorOut) {
    json parsed;
    try {
        parsed = json::parse(text);
    } catch (const std::exception& e) {
        errorOut = std::string("json parse: ") + e.what();
        return false;
    }

    DeviceConfigRoot tempRoot;
    MachinePointTable tempMachine;
    if (!parseDocument(parsed, tempRoot, tempMachine, errorOut, false)) return false;

    // 时间类下限 clamp(防运维把系统调挂)
    if (tempRoot.timing.faultReportIntervalMs < 100) tempRoot.timing.faultReportIntervalMs = 100;
    if (tempRoot.timing.valueChangeIntervalMs < 100) tempRoot.timing.valueChangeIntervalMs = 100;
    if (tempRoot.timing.dailyReportCheckIntervalMs < 1000) tempRoot.timing.dailyReportCheckIntervalMs = 1000;
    // 采集周期下限 20ms: 再密只是徒增串口压力(现场 PLC 用默认值 500ms 就够)。
    if (tempRoot.timing.acquisitionIntervalMs < 20) tempRoot.timing.acquisitionIntervalMs = 20;

    root_ = std::move(tempRoot);
    activeMachine_ = std::move(tempMachine);
    m_raw = std::move(parsed);
    m_rawText = text;
    m_revision = revisionOf(text);
    return true;
}

bool ConfigManager::validatePatch(const json& patch, const std::string& expectedRevision,
                                  json& merged, std::string& error) const {
    std::string currentText;
    std::string readError;
    if (!readWholeFile(m_path, currentText, readError)) {
        error = "读不到当前配置: " + readError;
        return false;
    }
    const std::string currentRev = revisionOf(currentText);
    if (!expectedRevision.empty() && expectedRevision != currentRev) {
        error = "配置文件已被其他人/手工修改(当前版本 " + currentRev +
                ", 页面基于 " + expectedRevision + "),请刷新后重试";
        return false;
    }

    json current;
    try {
        current = json::parse(currentText);
    } catch (const std::exception& e) {
        error = std::string("当前 config.json 不是合法 JSON: ") + e.what();
        return false;
    }

    merged = current;
    deepMerge(merged, patch);

    DeviceConfigRoot tempRoot;
    MachinePointTable tempMachine;
    return parseDocument(merged, tempRoot, tempMachine, error, true);
}

bool ConfigManager::savePatch(const json& patch, const std::string& expectedRevision,
                              const std::string& user, const std::string& clientIp,
                              std::string& error, std::string& newRevision) {
    json merged;
    if (!validatePatch(patch, expectedRevision, merged, error)) return false;

    std::string currentText;
    if (!readWholeFile(m_path, currentText, error)) return false;
    const std::string oldRev = revisionOf(currentText);
    const std::string newText = merged.dump(2);
    const std::string newRev = revisionOf(newText);

    std::error_code ec;
    std::filesystem::create_directories(historyDir(m_path), ec);

    const std::string histId = "config-" + timestampId() + "-" + oldRev.substr(0, 8) + ".json";
    const std::string histPath = (std::filesystem::path(historyDir(m_path)) / histId).string();
    if (!writeAtomic(histPath, currentText, error)) return false;
    if (!writeAtomic(lastGoodPath(m_path), currentText, error)) return false;
    if (!writeAtomic(m_path, newText, error)) return false;

    json meta = {
        {"revision", newRev},
        {"time", nowText()},
        {"user", user},
        {"ip", clientIp}
    };
    std::string metaError;
    if (!writeAtomic(metaPath(m_path), meta.dump(2), metaError)) {
        startupLog("[Config] meta write failed (config itself已写成功): " + metaError);
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_raw = std::move(merged);
        m_rawText = newText;
        m_revision = newRev;
        m_degraded = false;
    }
    newRevision = newRev;
    return true;
}

bool ConfigManager::rollback(const std::string& historyId, const std::string& user,
                             const std::string& clientIp, std::string& error,
                             std::string& newRevision) {
    if (historyId.empty() || historyId.find('/') != std::string::npos ||
        historyId.find('\\') != std::string::npos || historyId == "." || historyId == ".." ||
        historyId.find("..") != std::string::npos) {
        error = "非法的历史版本 ID";
        return false;
    }
    const std::filesystem::path histDir(historyDir(m_path));
    const std::filesystem::path histPath = histDir / historyId;
    std::string rollbackText;
    if (!readWholeFile(histPath.string(), rollbackText, error)) return false;

    json rollbackJson;
    try {
        rollbackJson = json::parse(rollbackText);
    } catch (const std::exception& e) {
        error = std::string("历史版本不是合法 JSON: ") + e.what();
        return false;
    }
    DeviceConfigRoot tempRoot;
    MachinePointTable tempMachine;
    if (!parseDocument(rollbackJson, tempRoot, tempMachine, error, true)) return false;

    std::string currentText;
    if (!readWholeFile(m_path, currentText, error)) return false;
    const std::string currentRev = revisionOf(currentText);
    const std::string newRev = revisionOf(rollbackText);
    const std::string safeId = "rollback-backup-" + timestampId() + "-" + currentRev.substr(0, 8) + ".json";

    std::error_code ec;
    std::filesystem::create_directories(histDir, ec);
    if (!writeAtomic((histDir / safeId).string(), currentText, error)) return false;
    if (!writeAtomic(lastGoodPath(m_path), currentText, error)) return false;
    if (!writeAtomic(m_path, rollbackText, error)) return false;

    json meta = {
        {"revision", newRev},
        {"time", nowText()},
        {"user", user},
        {"ip", clientIp},
        {"rollback", historyId}
    };
    if (!writeAtomic(metaPath(m_path), meta.dump(2), error)) return false;

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_raw = std::move(rollbackJson);
        m_rawText = rollbackText;
        m_revision = newRev;
        m_degraded = false;
    }
    newRevision = newRev;
    return true;
}

std::vector<ConfigManager::HistoryEntry> ConfigManager::history() const {
    std::vector<HistoryEntry> entries;
    const std::filesystem::path dir(historyDir(m_path));
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return entries;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json") continue;
        HistoryEntry item;
        item.id = name;
        item.time = name;
        item.size = static_cast<std::size_t>(entry.file_size(ec));
        entries.push_back(std::move(item));
    }
    std::sort(entries.begin(), entries.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        return a.id > b.id;
    });
    return entries;
}

void ConfigManager::markGood() {
    if (m_path.empty()) return;
    std::string text;
    std::string error;
    if (readWholeFile(m_path, text, error)) {
        writeAtomic(lastGoodPath(m_path), text, error);
    }
}

void ConfigManager::resolveEnvVars(std::string& v) {
    // 把 ${MQTT_PASSWORD} → getenv("MQTT_PASSWORD")
    if (v.size() < 4) return;
    auto start = v.find("${");
    auto end = v.find('}');
    if (start == std::string::npos || end == std::string::npos || end <= start + 2) return;
    std::string envVar = v.substr(start + 2, end - start - 2);
    const char* envVal = std::getenv(envVar.c_str());
    if (envVal) {
        v = envVal;
        startupLog("[Config] env var resolved: " + envVar);
    } else {
        // 环境变量未设:把 v 清空,避免把 ${...} 字面量发到 broker
        v.clear();
        startupLog("[Config] env var not set: " + envVar);
    }
}

void ConfigManager::dumpToLog() const {
    LOG_INFO("========== Config dump ==========");
    LOG_INFO("box.boxNo = " + root_.box.boxNo);
    LOG_INFO("box.dbPath = " + root_.box.dbPath);
    LOG_INFO("box.storageRoot = " + root_.box.storageRoot);
    LOG_INFO("box.logPath = " + root_.box.logPath);
    LOG_INFO("box.boxType = " + root_.box.boxType);
    LOG_INFO("stateMachine.mode = " + root_.stateMachine.mode);
    LOG_INFO("stateMachine.logStateTransition = " + std::string(root_.stateMachine.logStateTransition ? "true" : "false"));
    LOG_INFO("mqtt.brokerIp = " + root_.mqtt.brokerIp + ":" + std::to_string(root_.mqtt.port));
    LOG_INFO("mqtt.username = " + root_.mqtt.username);
    LOG_INFO("mqtt.topics.monitor = " + root_.mqtt.topics.monitor);
    LOG_INFO("mqtt.topics.command = " + root_.mqtt.topics.command);
    LOG_INFO("plcSerial.port = " + root_.plcSerial.port);
    LOG_INFO("plcSerial.baudRate = " + std::to_string(root_.plcSerial.baudRate));
    LOG_INFO("machine.machineType = " + activeMachine_.machineType);
    LOG_INFO("machine.protocol = " + activeMachine_.protocol);
    LOG_INFO("machine.signals size = " + std::to_string(activeMachine_.signals.size()));
    LOG_INFO("machine.services size = " + std::to_string(activeMachine_.services.size()));
    LOG_INFO("plcTcp.plcId = " + root_.plcTcp.plcId);
    LOG_INFO("plcTcp.host:port = " + root_.plcTcp.host + ":" + std::to_string(root_.plcTcp.port));
    LOG_INFO("plcTcp.unitId = " + std::to_string(root_.plcTcp.unitId));
    LOG_INFO("cameras.size = " + std::to_string(root_.cameras.size()));
    LOG_INFO("http.bind:port = " + root_.http.bind + ":" + std::to_string(root_.http.port));
    LOG_INFO("================================");
}
