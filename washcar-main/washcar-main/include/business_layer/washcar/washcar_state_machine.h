#pragma once

#include "data_layer/fx_plc/fx_plc_status.h"
#include "common/config/config_object.h"

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class IDeviceService;

enum class ServiceState { IDLE, RUNNING, FAULT };
enum class WashCarCommand { START, STOP, RESET, ENTER_MAINTENANCE, EXIT_MAINTENANCE, POINT_ON, POINT_OFF, QUERY };

struct MachineStatus {
    ServiceState state = ServiceState::IDLE;
    bool washing = false;         // 洗车标志
    bool hasFault = false;        // 故障位偏离正常值
    bool needsReset = false;      // 需要先复位(不改变状态,只影响复位命令)
    bool allowStart = false;      // 启动准入是否满足
    int faultCode = 0;            // 故障码
    std::string blockReason;      // 挡住启动的原因
};

struct WashCarCheckResult {
    bool allowed = false;
    std::string reason;
    std::string suggestion;
};

// 命令准入结果。放行时状态机已经"占位迁移"(并发第二发会被拒),
// 后续 PLC 动作失败必须调 rollback 退回原状态。
struct WashCarAdmission {
    bool blocked = false;                 // true = 被拒(enforce 模式)
    bool transitioned = false;            // true = 本次准入改了状态
    ServiceState previous = ServiceState::IDLE;
    ServiceState current = ServiceState::IDLE;
    std::string reason;
    std::string suggestion;
};

// 点位值表。键是 PLC 文档写法:"Y74" / "D28" / "M1"
struct PlcPointValues {
    std::unordered_map<std::string, int> values;
    std::string plcId;                    // 这组点位来自哪台 PLC(日志用)
    bool has(const std::string& point) const { return values.count(point) > 0; }
    int at(const std::string& point, int fallback = 0) const {
        auto entry = values.find(point);
        return entry == values.end() ? fallback : entry->second;
    }
};

PlcPointValues toPlcPointValues(const FxPlcStatus& status);
const char* toString(ServiceState state);

// 洗车机状态机。点位表说"有哪些点"(common/config/config_object.h),
// 子类说"怎么判";点位表的访问细节收在类里,外部只按信号名/服务名用。
class WashCarStateMachine {
public:
    explicit WashCarStateMachine(const std::string& boxType);
    virtual ~WashCarStateMachine() = default;

    // ===== 状态查询:这台机器现在怎么样 =====
    // 当前命令态(空闲/运行/故障)。只由 admit / rollback / PLC 故障升级改。
    ServiceState state() const;
    virtual MachineStatus buildStatus(const PlcPointValues& points) const;
    // 状态查询用的一句话
    std::string describeNow(const PlcPointValues& points, bool maintenance);

    // ===== 命令入口:能不能发,失败时回滚 =====
    virtual bool checkStart(const PlcPointValues& points, WashCarCheckResult& result) const;
    // 命令准入: 判状态/准入, 放行时立刻迁移状态(防连点)。
    // log_only 只记日志不拦, 但仍按合法方向迁移, 保证状态和实际接受的命令一致。
    WashCarAdmission admit(const PlcPointValues& points, bool maintenance,
                           WashCarCommand command, const char* commandName);
    // PLC 动作失败时退回 admit 占的位
    void rollback(const WashCarAdmission& admission, const char* commandName);

    // ===== 上报报文:给云端组 JSON =====
    virtual nlohmann::json buildFaultReport(const PlcPointValues& points) const = 0;
    virtual nlohmann::json buildValueChangeReport(const PlcPointValues& points) const;
    // 工序名与工序号。驿公里按 S 步号给;海天直接用 D142 的步骤号,不实现。
    virtual std::string stepName(int step) const;
    virtual int stepNumber(int step) const;

    // ===== 点位表访问:信号名 ↔ PLC地址 ↔ 云端字段,以及按地址读写 =====
    // signalName 是软元件的内部语义名(如 "washing" / "fault")
    std::string signalPoint(const std::string& signalName) const;   // → PLC 软元件地址("D28")
    std::string cloudKey(const std::string& signalName) const;      // → 上报 JSON 字段名("fault")
	std::string servicePoint(const std::string& serviceName,        // 取默认值或其他模式(如驿公里多种启动模式)
                              const std::string& selector = "default") const;
    bool readPoint(const PlcPointValues& points, const std::string& point, int& out) const;
    bool writePoint(IDeviceService& deviceService, const std::string& plcId,
                    const std::string& point, bool value) const;

    // ===== 维修模式标志持久化 =====
    static bool loadMaintenanceFlag(const std::string& storageRoot);
    static bool saveMaintenanceFlag(const std::string& storageRoot, bool maintaining);

protected:

    virtual bool isWashing(const PlcPointValues& points) const = 0;
    // 当前工序步号(给 activeStep 规则用)。没有步号语义的机型返回 -1。(仅驿公里)
    virtual int activeStep(const PlcPointValues& points) const { (void)points; return -1; }
    // 洗车程序是否已经跑完。只看最终位判断
    virtual bool isWashCompleted(const PlcPointValues& points) const { (void)points; return false; }

    int getSignalValue(const PlcPointValues& points, const std::string& name) const;// 拿软元件值
    bool isSignalNormal(const PlcPointValues& points, const std::string& name) const;// 判断异常
    std::string describe(const MachineStatus& status, bool maintenance) const;// 状态机日志

    // 关键点位变化打印日志
    void logKeyStateChanges(const PlcPointValues& changedPoints) const;

    const washcar::cfg::MachinePointTable& pointTable_;

private:
    // 通用命令策略(状态规则 + 启动准入)。启动物理准入交给子类。
    WashCarCheckResult checkCommand(const PlcPointValues& points, const MachineStatus& status,
        bool maintenance, WashCarCommand command) const;

    MachineStatus buildStatusWith(const PlcPointValues& points, ServiceState state) const;
    // 按迁移表改状态; 返回是否真的迁移了
    bool applyTransition(WashCarCommand command);
    void setStateLocked(ServiceState next, const char* trigger) const;

    mutable std::recursive_mutex m_stateMutex;
    mutable ServiceState m_state = ServiceState::IDLE;
    // 运行态里"真的洗过车"的标志: 只有见过 washing=1 才允许因"洗完"自动回空闲,
    // 否则刚下发启动、PLC 还没置洗车标志的那一两秒会被误判成"洗完了"。
    mutable bool m_sawWashing = false;
};

// 海天(FX 串口):Y74 = 洗车标志,D28 != 0 = 故障码。
// 启动准入规则在点位表 startChecks 里(内置默认对应 M187 / M1 / X62 / X63 / X46)。
class HaiTianStateMachine : public WashCarStateMachine {
public:
    HaiTianStateMachine();

    nlohmann::json buildFaultReport(const PlcPointValues& points) const override;

protected:
    bool isWashing(const PlcPointValues& points) const override;
    bool isWashCompleted(const PlcPointValues& points) const override;
    // 当前 S 步号(没有 S 数据返回 -1), 给 activeStep 规则用
};

// 驿公里(Modbus TCP):S 步号落在 S20~S80 = 洗车工序中,D1000 != 0 = 故障码。
// 启动准入规则在点位表 startChecks 里(内置默认: 当前步 S5 / M510=0 / M282=1)。
class YiGongLiStateMachine : public WashCarStateMachine {
public:
    YiGongLiStateMachine();

    nlohmann::json buildFaultReport(const PlcPointValues& points) const override;
    nlohmann::json buildValueChangeReport(const PlcPointValues& points) const override;

    int scanActiveStep(const PlcPointValues& points) const;
    std::string stepName(int step) const override;
    int stepNumber(int step) const override;

protected:
    bool isWashing(const PlcPointValues& points) const override;
    bool isWashCompleted(const PlcPointValues& points) const override;
    // 当前 S 步号(没有 S 数据返回 -1), 给 activeStep 规则用
    int activeStep(const PlcPointValues& points) const override;
};
