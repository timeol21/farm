#pragma once

#include "common/thread/thread_manager.h"

// 洗车业务接口与公共实现。
//
// 业务层做三件事：
//   1. 收业务消息（BusinessCommand），按命令类型调对应方法
//   2. 判状态 / 校验准入（WashCarStateMachine）
//   3. 组织要上报的内容，交给 IReportSink —— 不关心 topic、不关心 MQTT
//
// 数据来源是 IDeviceService（data_layer 提供的接口），只拿到 PlcPointValues，
// 不认识 PLC 协议、不认识采集线程。

#include "business_layer/washcar/washcar_operation.h"
#include "business_layer/washcar/washcar_state_machine.h"
#include "common/config/config_manager.h"
#include "common/message.h"
#include "common/plc_point.h"
#include "data_layer/idevice_service.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

class IWashCarService {
public:
    virtual ~IWashCarService() = default;

    // 洗车命令
    virtual WashCarOperationResult startWashCar(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult stopWashCar(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult resetWashCar(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) = 0;

    /// 按点位名采样当前值。points 为空 = 采点位表里配置的全部点。
    /// readdata 走这条，和"查询业务状态"分开：后者带状态机结论，前者只要原始值。
    virtual WashCarOperationResult readPoints(const std::vector<std::string>& points) = 0;

    // 机型特有命令（默认实现直接拒绝，驿公里覆盖）
    virtual WashCarOperationResult abortWashCar(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult stopWork(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult stopService(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult resetSafe(const WashCarOperation& operation) = 0;

    virtual WashCarOperationResult clearAlarm(const WashCarOperation& operation) = 0;
    virtual bool changeMode(const WashCarOperation& operation) = 0;
    virtual bool isWashing() const = 0;

    // 单点操作
    virtual WashCarOperationResult pointOn(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult pointOff(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult pointReset(const WashCarOperation& operation) = 0;
    virtual WashCarOperationResult pointStatus(const WashCarOperation& operation) = 0;

    // 洗车进度轮询（monitor）
    virtual void startWashCarStatusMonitor(const WashCarOperation& operation) = 0;
    virtual void stopWashCarStatusMonitor() = 0;

    // 三路主动上报，由业务层自己的定时器调用
    virtual void executeDailyReportTask() = 0;
    virtual void executeValueChangeTask() = 0;
    virtual void executeFaultReportTask() = 0;
};

// 海天和驿公里走同一套流程，机型差异收在虚函数里。
class WashCarServiceBase : public IWashCarService {
public:
    WashCarServiceBase(IDeviceService& deviceService,
                       IReportSink& reportSink,
                       std::unique_ptr<WashCarStateMachine> stateMachine,
                       std::string machineLabel);
    ~WashCarServiceBase() override = default;

    WashCarOperationResult pointOn(const WashCarOperation& operation) override;
    WashCarOperationResult pointOff(const WashCarOperation& operation) override;
    WashCarOperationResult pointStatus(const WashCarOperation& operation) override;
    WashCarOperationResult readPoints(const std::vector<std::string>& points) override;
    bool changeMode(const WashCarOperation& operation) override;

    void executeValueChangeTask() override;
    void stopWashCarStatusMonitor() override;

    // 本机型不支持的命令，默认拒掉
    WashCarOperationResult abortWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult stopWork(const WashCarOperation& operation) override;
    WashCarOperationResult stopService(const WashCarOperation& operation) override;
    WashCarOperationResult resetSafe(const WashCarOperation& operation) override;

    // 机型差异
    WashCarOperationResult startWashCar(const WashCarOperation& operation) override = 0;
    WashCarOperationResult stopWashCar(const WashCarOperation& operation) override = 0;
    WashCarOperationResult resetWashCar(const WashCarOperation& operation) override = 0;
    WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override = 0;
    WashCarOperationResult clearAlarm(const WashCarOperation& operation) override = 0;
    WashCarOperationResult pointReset(const WashCarOperation& operation) override = 0;
    bool isWashing() const override = 0;
    void startWashCarStatusMonitor(const WashCarOperation& operation) override = 0;
    void executeDailyReportTask() override = 0;
    void executeFaultReportTask() override = 0;

protected:
    /// 取本机 PLC 的点位快照。首轮采集没完成时返回 false。
    /// 两机型实现一致（单机场景就是唯一那台），所以不做虚函数。
    bool frontPoints(PlcPointValues& out);

    /// pointOff 之后的机型收尾（驿公里清暂停标志）
    virtual void afterPointOff(const std::string& plcId, int pointDecimal, bool success) {}

    WashCarOperationResult rejectUnsupported(const WashCarOperation& operation,
                                             ErrorCode::Code code,
                                             const char* commandName);

    /// 上报的唯一出口。业务层只说"哪一类"，通讯层负责选 endpoint。
    void sendReport(ReportKind kind, const std::string& payload);

    IDeviceService& deviceService;
    IReportSink& reportSink;

    std::unique_ptr<WashCarStateMachine> m_stateMachine;
    std::string m_machineLabel;

    bool m_isMaintaining = false;
    bool m_isJogExecuting = false;
    bool m_statusMonitorRunning = false;

    json m_lastFaultReport;
    json m_lastValueChangeReport;

    WashCarOperation m_monitorOperation;
    ManagedThread m_monitorThread;
    std::atomic<bool> m_monitorStopFlag{false};

    WashCarOperationResult m_currentStatus;

    std::string getServicePoint(const std::string& service,
                                const std::string& selector = "default");

    bool checkStateMachine(WashCarCommand command, const char* commandName);
    bool canExecuteCloudCommand() const;
    bool canExecuteJogCommand() const;
    void setJogExecuting(bool executing);

    void collectDailyReportPoints(std::vector<std::pair<std::string, int>>& points);
    std::vector<std::string> collectConfiguredPoints();

    static std::string toFlexemPointName(const std::string& key);
    static void addCommonFlexemFields(json& msg, const std::string& boxNo);
};

class HaiTianWashCarService : public WashCarServiceBase {
public:
    HaiTianWashCarService(IDeviceService& deviceService, IReportSink& reportSink);
    ~HaiTianWashCarService();

    WashCarOperationResult startWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult stopWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult resetWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override;
    WashCarOperationResult clearAlarm(const WashCarOperation& operation) override;
    WashCarOperationResult pointReset(const WashCarOperation& operation) override;
    bool isWashing() const override;
    void startWashCarStatusMonitor(const WashCarOperation& operation) override;
    void executeDailyReportTask() override;
    void executeFaultReportTask() override;

private:
    // 物料不足当天只上报一次
    bool m_m257ExceptionReportedToday = false;
    bool m_m259ExceptionReportedToday = false;
};

class YiGongLiWashCarService : public WashCarServiceBase {
public:
    YiGongLiWashCarService(IDeviceService& deviceService, IReportSink& reportSink);
    ~YiGongLiWashCarService();

    WashCarOperationResult startWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult stopWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult resetWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override;
    WashCarOperationResult clearAlarm(const WashCarOperation& operation) override;
    WashCarOperationResult pointReset(const WashCarOperation& operation) override;
    bool isWashing() const override;
    void startWashCarStatusMonitor(const WashCarOperation& operation) override;
    void executeDailyReportTask() override;
    void executeFaultReportTask() override;

    // 驿公里独有命令
    WashCarOperationResult abortWashCar(const WashCarOperation& operation) override;
    WashCarOperationResult stopWork(const WashCarOperation& operation) override;
    WashCarOperationResult stopService(const WashCarOperation& operation) override;
    WashCarOperationResult resetSafe(const WashCarOperation& operation) override;

protected:
    void afterPointOff(const std::string& plcId, int pointDecimal, bool success) override;

private:
    int m_activeS = -1;
    std::string m_deviceStatusStr;
    int m_lastKnownD1000 = 0;
    bool m_isServicePaused = false;

    int scanActiveS(const PlcPointValues& points) const;
    std::string parseD1210(const PlcPointValues& points) const;
    bool waitForConfirm(const std::function<bool(const PlcPointValues&)>& check);
    void updateLastKnownState(const PlcPointValues& points);

    int pickStartModeAddress(const WashCarOperation& operation) const;
    int pickResetModeAddress(const WashCarOperation& operation) const;

    bool writePointAndReport(const WashCarOperation& operation, int mAddr, bool value,
                             int& outCode, std::string& outMessage);
};
