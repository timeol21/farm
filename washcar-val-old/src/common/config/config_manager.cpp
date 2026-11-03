#include "common/config/config_manager.h"
#include "common/config/config_serialization.h"
#include "common/config/default_point.h"
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

using json = nlohmann::json;
using namespace washcar::cfg;

// 配置加载发生在日志系统起来之前(日志目录本身来自配置),
// 这时候 LOG_* 会被 LoggerManager 丢掉("work" logger 还没注册),
// 所以启动期这几条消息走 stderr,现场在控制台/journal 里能直接看到。
static void startupLog(const std::string& msg) {
    std::cerr << "[Config] " << msg << std::endl;
}

namespace {

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

    // 1) 采集点位:格式合法 + 收进已采集合
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
            acquiredPoints.insert(point);
        }
    }

    // 2) 信号: name 唯一 + 点位格式合法 + 点位在采集清单里
    std::set<std::string> signalNames;
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

void ConfigManager::applyDefaults() {
    root_ = default_config::get();
    // 默认表里两台机器都装了,按 boxType 取当前生效那份
    const auto it = root_.machines.find(root_.box.boxType);
    activeMachine_ = (it != root_.machines.end()) ? it->second
                                                  : default_point::getPointTable(root_.box.boxType);
}

bool ConfigManager::load(const std::string& path, std::string& errorOut) {
    // 1) 先全部用默认值兜底,保证加载失败也能起
    applyDefaults();

    // 2) 读 JSON;分 4 情况(spec §16.5):
    //    A. 文件不存在          → 生成模板 + applyDefaults(),返回 true(WARN)
    //    B. 文件存在但 size==0  → 覆盖生成模板 + applyDefaults(),返回 true(WARN)
    //    C. 文件存在但解析失败  → 不动文件 + applyDefaults(),返回 false(ERROR)
    //    D. 文件存在且解析成功  → 正常路径,不动文件
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        // 情况 A:首次部署 — 写模板,系统用默认值起,运维改完模板重启即可
        std::string werr;
        if (!writeTemplate(path, werr)) {
            errorOut = "cannot write template at " + path + ": " + werr;
            startupLog(errorOut);
            return false;   // exe 目录不可写(只读 mount 等),拒启动
        }
        startupLog("[Config] generated template at " + path);
        startupLog("[Config] please edit brokerIp / RTSP URL / boxNo then restart");
        return true;
    }

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size == 0) {
        // 情况 B:空文件(异常中断 / 手工清空) — 覆盖生成模板
        in.close();
        std::string werr;
        if (!writeTemplate(path, werr)) {
            errorOut = "cannot rewrite empty file at " + path + ": " + werr;
            startupLog(errorOut);
            return false;
        }
        startupLog("[Config] file empty, regenerated template at " + path);
        return true;
    }

    std::stringstream ss; ss << in.rdbuf();
    std::string text = ss.str();
    in.close();

    // 3) 解析
    if (!parseJson(text, errorOut)) {
        // 情况 C:解析失败 — 关键:不动文件,运维手工改的不会被覆盖
        startupLog("parse failed: " + errorOut
                  + ", fallback to defaults, fix the file before next restart");
        applyDefaults();
        return false;
    }

    // 4) 环境变量替换(密码等敏感字段)
    resolveEnvVars(root_.mqtt.password);

    // 5) 关键字段强校验 — 缺失则系统拒启动(parseJson 已做 contains 检查,这里是双保险)
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

    // 情况 D:正常路径
    startupLog("[Config] loaded from " + path
             + ", cameras=" + std::to_string(root_.cameras.size())
             + ", machineType=" + activeMachine_.machineType);
    return true;
}

bool ConfigManager::writeTemplate(const std::string& path, std::string& errorOut) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        errorOut = std::strerror(errno);
        return false;
    }
    try {
        out << default_config::toJson();
    } catch (const std::exception& e) {
        errorOut = std::string("toJson threw: ") + e.what();
        return false;
    }
    out.close();
    return out.good();
}

const MachinePointTable& ConfigManager::machine(const std::string& boxType) const {
    const auto entry = root_.machines.find(boxType);
    if (entry != root_.machines.end()) return entry->second;
    return default_point::getPointTable(boxType);
}

bool ConfigManager::parseJson(const std::string& text, std::string& errorOut) {
    try {
        auto parsed = json::parse(text);

        // --- 逐子结构增量覆盖(缺省键保留现值; 键名/数组整体替换语义见 config_serialization.h) ---
        from_json(parsed, root_);

        // --- 关键字段缺失检测(spec §10.3:boxNo / brokerIp 缺失必须拒启动) ---
        // 用 j.contains 区分"JSON 里就没这个 key"vs"JSON 里写了但值为空" —
        // 即便默认值是 "123456",只要 JSON 里没写 boxNo,就视为缺失。
        if (!parsed.contains("box") || !parsed["box"].contains("boxNo") || root_.box.boxNo.empty()) {
            errorOut = "box.boxNo missing or empty in JSON";
            return false;
        }
        if (!parsed.contains("mqtt") || !parsed["mqtt"].contains("brokerIp") || root_.mqtt.brokerIp.empty()) {
            errorOut = "mqtt.brokerIp missing or empty in JSON";
            return false;
        }

        // --- 洗车机软元件表 ---
        // "machine" 段按 boxType 分段:{"haiTian": {...}, "yiGongLi": {...}}。
        // 当前生效的那一份 = box.boxType 对应的那段; 没配就用 default_point 的内置表。
        // 校验不过或 machineType 跟 boxType 对不上 → 回退内置表,记 ERROR,但不拦启动
        // (跟整体配置策略一致:配置坏了也能起,用默认值兜底)。
        {
            const MachinePointTable fallback = default_point::getPointTable(root_.box.boxType);
            std::string fallbackErr;
            if (!validatePointTable(fallback, fallbackErr)) {
                startupLog("[Config] 内置软元件表不合法 boxType=" + root_.box.boxType
                           + ": " + fallbackErr);
            }

            const auto it = root_.machines.find(root_.box.boxType);
            if (it == root_.machines.end()) {
                activeMachine_ = fallback;
                startupLog("[Config] machine 段没有 " + root_.box.boxType
                           + ",使用内置默认表, signals=" + std::to_string(activeMachine_.signals.size()));
            } else {
                std::string customErr;
                if (it->second.machineType != root_.box.boxType) {
                    startupLog("[Config] machine." + root_.box.boxType + ".machineType="
                               + it->second.machineType + " 与 box.boxType 不一致,回退内置表");
                    activeMachine_ = fallback;
                } else if (!validatePointTable(it->second, customErr)) {
                    startupLog("[Config] machine." + root_.box.boxType
                               + " 校验不通过,回退内置表: " + customErr);
                    activeMachine_ = fallback;
                } else {
                    activeMachine_ = it->second;
                    startupLog("[Config] machine." + root_.box.boxType
                               + " 来自 config.json, signals=" + std::to_string(activeMachine_.signals.size()));
                }
            }
        }

        // --- 时间类下限 clamp(防运维把系统调挂) ---
        //   faultReportIntervalMs / valueChangeIntervalMs < 100ms → 100ms,防刷屏
        //   dailyReportCheckIntervalMs < 1000ms → 1000ms,1 分钟精度不需要更细
        if (root_.timing.faultReportIntervalMs < 100) {
            startupLog("[Config] faultReportIntervalMs=" + std::to_string(root_.timing.faultReportIntervalMs)
                        + " too small, clamped to 100ms");
            root_.timing.faultReportIntervalMs = 100;
        }
        if (root_.timing.valueChangeIntervalMs < 100) {
            startupLog("[Config] valueChangeIntervalMs=" + std::to_string(root_.timing.valueChangeIntervalMs)
                        + " too small, clamped to 100ms");
            root_.timing.valueChangeIntervalMs = 100;
        }
        if (root_.timing.dailyReportCheckIntervalMs < 1000) {
            startupLog("[Config] dailyReportCheckIntervalMs=" + std::to_string(root_.timing.dailyReportCheckIntervalMs)
                        + " too small, clamped to 1000ms");
            root_.timing.dailyReportCheckIntervalMs = 1000;
        }

        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("json parse: ") + e.what();
        return false;
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
