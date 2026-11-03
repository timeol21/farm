#pragma once
#include "common/config/config_object.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstddef>
#include <mutex>

// 单例,跟项目里 DatabaseManager::instance() / LoggerManager::instance() 风格一致
namespace washcar {
namespace cfg {
class ConfigManager {
public:
    static ConfigManager& instance();

    // 启动时调一次。返回 false 表示关键字段缺失,系统不能起
    // 文件不存在 / JSON 解析失败 → 走默认值 + WARN,不致命
    bool load(const std::string& jsonPath, std::string& errorOut);

    // 默认值兜底(单独暴露,便于测试)
    void applyDefaults();

    // 字段访问 — 全部 inline getter,不改 .h 不会触发下游重编
    const DeviceConfigRoot&    root()       const { return root_; }
    const MqttConfig&          mqtt()       const { return root_.mqtt; }
    const BoxConfig&           box()        const { return root_.box; }
    const PlcSerialConfig&     plcSerial()  const { return root_.plcSerial; }
    const HttpConfig&          http()       const { return root_.http; }
    const TimingConfig&        timing()     const { return root_.timing; }
    const PlcTcpConfig&        plcTcp()     const { return root_.plcTcp; }
    const AiConfig&           ai()         const { return root_.ai; }
    const StateMachineConfig&    stateMachine() const { return root_.stateMachine; }
    // 当前生效的洗车机软元件表。按 box.boxType 从 root_.machines 里选;
    // 该机型没配时回落到 default_point.h 的内置默认表。
    const MachinePointTable&      machine()    const { return activeMachine_; }
    // 指定机型的软元件表。没配就用 default_point.h 的内置默认表。
    // 洗车机状态机按自己的机型来取 —— 状态机只认自己那台机器的表。
    const MachinePointTable&      machine(const std::string& boxType) const;
    const std::vector<CameraConfig>& cameras() const { return root_.cameras; }
    const AdminConfig& admin() const { return root_.admin; }

    // ============ 配置台(本地运维页面)用 ============
    // 当前配置文件路径 / 原始 JSON(保留 ${ENV} 占位和未知字段) / 版本号
    const std::string& path() const { return m_path; }
    nlohmann::json raw() const;
    std::string revision() const;
    bool degraded() const { return m_degraded; }

    // 校验 patch(合并到当前 config.json 后严格校验),不写盘
    bool validatePatch(const nlohmann::json& patch, const std::string& expectedRevision,
                       nlohmann::json& merged, std::string& error) const;

    // 校验 + 备份 + 原子写 + 更新 meta。返回 false 时 error 里是给人看的原因。
    bool savePatch(const nlohmann::json& patch, const std::string& expectedRevision,
                   const std::string& user, const std::string& clientIp,
                   std::string& error, std::string& newRevision);

    // 从 config_history/ 里的文件回滚
    bool rollback(const std::string& historyId, const std::string& user, const std::string& clientIp,
                  std::string& error, std::string& newRevision);

    struct HistoryEntry {
        std::string id;
        std::string time;
        std::size_t size = 0;
    };
    std::vector<HistoryEntry> history() const;

    // 进程正常启动后调用:把当前 config.json 复制成 config.lastgood.json。
    // 下次 config.json 解析失败时用它兜底,避免手改坏一个字符就起不来。
    void markGood();

    // 调试用:把当前生效配置 dump 到日志
    void dumpToLog() const;

    // ============ 目录口径的唯一出处 ============
    // main 装配和检测服务都从这里取,别在各自文件里再写一遍 storageRoot/dbPath 的回退规则。
    //   storageRoot 空 → 回退 dbPath(现场老 config.json 只有 dbPath 这个字段)
    //   logPath    空 → 回退 <storageRoot>/log
    // 返回值可能是相对路径(相对 exe 目录,由 main chdir 保证),调用方需要绝对路径自己再 resolve。
    std::string storageDir() const {
        return root_.box.storageRoot.empty() ? root_.box.dbPath : root_.box.storageRoot;
    }
    std::string logDir() const {
        return root_.box.logPath.empty() ? storageDir() + "/log" : root_.box.logPath;
    }
    // 日志级别(debug/info/warning/error); 认不出来按 info
    std::string logLevel() const {
        const std::string& l = root_.box.logLevel;
        return l.empty() ? std::string("info") : l;
    }
    // 告警图片目录;storageDir 为空(没有可用持久化根)时返回空串 = 不落盘
    std::string imagesDir() const {
        return storageDir().empty() ? std::string() : storageDir() + "/images";
    }

    // 仅供单元测试切换 stateMachine.mode。
    // 改的是真实 ConfigManager 实例，守卫类读的就是它，不是替身。
    void setStateMachineModeForTest(const std::string& mode) { root_.stateMachine.mode = mode; }
    // 单测用: 直接换掉当前生效点位表(验证"准入规则由数据驱动")
    void setActiveMachineForTest(const MachinePointTable& table) { activeMachine_ = table; }

private:
    ConfigManager() = default;
    bool parseJson(const std::string& text, std::string& errorOut);
    void resolveEnvVars(std::string& value);   // 替换 ${ENV_VAR}
    // 写默认 JSON 模板到 path,返回 true 表示写成功;errno 信息写到 errorOut
    bool writeTemplate(const std::string& path, std::string& errorOut);

    DeviceConfigRoot root_;
    // 按 box.boxType 选出来的当前生效软元件表(见 machine() 的说明)
    MachinePointTable activeMachine_;

    std::string m_path;
    std::string m_rawText;
    nlohmann::json m_raw = nlohmann::json::object();
    std::string m_revision;
    bool m_degraded = false;
    mutable std::mutex m_stateMutex;
};

}  // namespace cfg
}  // namespace washcar

// 让业务模块不必每个调用都写 washcar::cfg::ConfigManager
using ConfigManager = washcar::cfg::ConfigManager;

namespace washcar {
namespace cfg {
// 校验一份洗车机点位表自洽。返回 false 时 errorOut 里写清楚哪一条不通过。
// 内容: name 唯一 / point 格式合法 / point 在采集清单里 /
//        故障位·归位传感器·上报清单引用的 name 存在 / 采集读次数不超预算。
bool validatePointTable(const MachinePointTable& pointTable, std::string& errorOut);
}  // namespace cfg
}  // namespace washcar