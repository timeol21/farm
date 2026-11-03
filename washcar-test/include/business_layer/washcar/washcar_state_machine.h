#pragma once

#include "common/config/config_object.h"
#include "common/plc_point.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

class IDeviceService;

enum class ServiceState { IDLE, WORKING, FAULT };
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

struct WashCarCommandResult {
    bool allowed = false;
    std::string reason;
    std::string suggestion;
};

// 点位值表用 common/plc_point.h 的 PlcPointValues（键是 PLC 文档写法："Y74" / "D28" / "M1"）

const char* toString(ServiceState state);

// 洗车机状态机。点位表说"有哪些点"(common/config/config_object.h),
// 子类说"怎么判";点位表的访问细节收在类里,外部只按信号名/服务名用。
class WashCarStateMachine {
public:
    explicit WashCarStateMachine(const std::string& boxType);
    virtual ~WashCarStateMachine() = default;

    // 状态怎么推、启动准入满不满足
    virtual MachineStatus buildStatus(const PlcPointValues& points) const;
    virtual bool checkStart(const PlcPointValues& points, WashCarCommandResult& result) const = 0;
    // 故障报文(pushalarm)。海天稠密:事件类点位全带;驿公里稀疏:偏离正常值才带。
    virtual nlohmann::json buildFaultReport(const PlcPointValues& points) const = 0;
    // 属性类点位的变化报文(monitor / readdata)。默认只带变化的点位。
    virtual nlohmann::json buildValueChangeReport(const PlcPointValues& points) const;

    // 点位表访问
    bool readPoint(const PlcPointValues& points, const std::string& point, int& out) const;
    bool writePoint(IDeviceService& deviceService, const std::string& plcId,
                    const std::string& point, bool value) const;
    std::string servicePoint(const std::string& serviceName,
                              const std::string& selector = "default") const;
    std::string cloudKey(const std::string& signalName) const;
    std::string signalPoint(const std::string& signalName) const;

    // 工序名与工序号。驿公里按 S 步号给;海天直接用 D142 的步骤号,不实现。
    virtual std::string stepName(int step) const;
    virtual int stepNumber(int step) const;

    // 通用命令策略。启动准入交给子类。
    WashCarCommandResult checkCommand(const PlcPointValues& points, const MachineStatus& status,
                                      bool maintenance, WashCarCommand command) const;
    // 命令入口守卫。返回 true = 拒绝。log_only 只记日志,enforce 才真拒。
    bool checkAndApply(const PlcPointValues& points, bool maintenance,
                       WashCarCommand command, const char* commandName);
    // 复位下发成功后调用:归位传感器全到位 + 无故障
    void noteResetConfirmed(const PlcPointValues& points, bool maintenance);
    // 状态查询用的一句话
    std::string describeNow(const PlcPointValues& points, bool maintenance);

    static bool loadMaintenanceFlag(const std::string& storageRoot);
    static bool saveMaintenanceFlag(const std::string& storageRoot, bool maintaining);

protected:
    // 这台机器现在算不算在洗
    virtual bool isWashing(const PlcPointValues& points) const = 0;

    int getSignalValue(const PlcPointValues& points, const std::string& name) const;
    bool isSignalNormal(const PlcPointValues& points, const std::string& name) const;
    std::string describe(const MachineStatus& status, bool maintenance) const;

    const washcar::cfg::MachinePointTable& pointTable_;
};

// 海天(FX 串口):Y74 = 洗车标志,D28 != 0 = 故障码。
// 启动准入 4 条(照搬原服务层 startWashCar 的前置检查,message 字面值也照搬):
//   M187 车辆已停放到位 / M1 启动前置条件 / X62 X63 左右防撞杆 / X46 车辆高度
class HaiTianStateMachine : public WashCarStateMachine {
public:
    HaiTianStateMachine();

    bool checkStart(const PlcPointValues& points, WashCarCommandResult& result) const override;
    nlohmann::json buildFaultReport(const PlcPointValues& points) const override;

protected:
    bool isWashing(const PlcPointValues& points) const override;
};

// 驿公里(Modbus TCP):S 步号落在 S20~S81 = 洗车工序中,D1000 != 0 = 故障码。
// 启动准入 3 条(照搬原服务层 startWashCar 的前置检查,message 字面值也照搬):
//   当前 S 步必须是 S5 / M510 = 0 / M282 = 1
class YiGongLiStateMachine : public WashCarStateMachine {
public:
    YiGongLiStateMachine();

    bool checkStart(const PlcPointValues& points, WashCarCommandResult& result) const override;
    nlohmann::json buildFaultReport(const PlcPointValues& points) const override;
    nlohmann::json buildValueChangeReport(const PlcPointValues& points) const override;

    int scanActiveStep(const PlcPointValues& points) const;
    std::string stepName(int step) const override;
    int stepNumber(int step) const override;

protected:
    bool isWashing(const PlcPointValues& points) const override;
};
