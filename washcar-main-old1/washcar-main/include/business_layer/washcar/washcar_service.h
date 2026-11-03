#pragma once
#include "business_layer/device/device_service.h"
#include "business_layer/lobby/lobby_object.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "common/config/config_manager.h"
#include "data_layer/fx_plc/fx_plc_status.h"
#include "business_layer/washcar/washcar_state_machine.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <nlohmann/json.hpp>

class IWashCarService {
public:
	// 定义一个函数类型，用于发送MQTT消息
	using MqttSender = std::function<void(const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType type)>;

	virtual ~IWashCarService() = default;

	// 启动洗车
	virtual WashCarOperationResult startWashCar(const WashCarOperation& operation) = 0;

	// 停止洗车
	virtual WashCarOperationResult stopWashCar(const WashCarOperation& operation) = 0;

	// 复位洗车系统
	virtual WashCarOperationResult resetWashCar(const WashCarOperation& operation) = 0;

	// 获取洗车状态
	virtual WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) = 0;

	// 终止洗车 (m206)
	virtual WashCarOperationResult abortWashCar(const WashCarOperation& operation) = 0;
	// 停止营业 (m501)
	virtual WashCarOperationResult stopWork(const WashCarOperation& operation) = 0;
	// 停止营业 2 (m510)
	virtual WashCarOperationResult stopService(const WashCarOperation& operation) = 0;
	// 安全复位 (m203)
	virtual WashCarOperationResult resetSafe(const WashCarOperation& operation) = 0;

	// 报警故障清除（按下急停后下发复位指令）
	virtual WashCarOperationResult clearAlarm(const WashCarOperation& operation) = 0;

	// 改变维护状态（进入/退出web维护模式）
	virtual bool changeMode(const WashCarOperation& operation) = 0;

	// 是否正在洗车中
	virtual bool isWashing() const = 0;

	//点位操作
	virtual WashCarOperationResult pointOn(const WashCarOperation& operation) = 0;
	virtual WashCarOperationResult pointOff(const WashCarOperation& operation) = 0;
	virtual WashCarOperationResult pointReset(const WashCarOperation& operation) = 0;
	virtual WashCarOperationResult pointStatus(const WashCarOperation& operation) = 0;

	// 监控洗车状态（持续轮询）
	virtual void startWashCarStatusMonitor(const WashCarOperation& operation, MqttSender sender) = 0;
	virtual void stopWashCarStatusMonitor() = 0;

	// 定时上报（每天8点，异常只上报一次）
	virtual void executeDailyReportTask(MqttSender sender) = 0;

	// 变化上报（M/X/S点位变化）
	virtual void executeValueChangeTask(MqttSender sender) = 0;

	// 故障上报（M/X/S点位变化）
	virtual void executeFaultReportTask(MqttSender sender) = 0;
};

// 洗车服务的公共实现。海天和驿公里走同一套流程,机型差异收在虚函数里。
// 默认把"本机型不支持的命令"直接拒掉,驿公里覆盖对应的几个。
class WashCarServiceBase : public IWashCarService {
public:
	WashCarServiceBase(IDeviceService& deviceService,
	                   std::unique_ptr<WashCarStateMachine> stateMachine,
	                   std::string machineLabel);
	~WashCarServiceBase() override = default;

	// 点位操作
	WashCarOperationResult pointOn(const WashCarOperation& operation) override;
	WashCarOperationResult pointOff(const WashCarOperation& operation) override;
	WashCarOperationResult pointStatus(const WashCarOperation& operation) override;

	// 改变维护状态（进入/退出web维护模式）
	bool changeMode(const WashCarOperation& operation) override;

	// 变化上报（M/X/S点位变化）
	void executeValueChangeTask(MqttSender sender) override;

	void stopWashCarStatusMonitor() override;

	// 本机型不支持的命令,默认拒掉
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
	void startWashCarStatusMonitor(const WashCarOperation& operation, MqttSender sender) override = 0;
	void executeDailyReportTask(MqttSender sender) override = 0;
	void executeFaultReportTask(MqttSender sender) override = 0;

protected:
	// 取这台机器的 PLC 状态
	virtual bool frontFxStatus(FxPlcStatus& out) = 0;
	// pointOff 之后的机型收尾(驿公里清暂停标志)
	virtual void afterPointOff(const std::string& plcId, int pointDecimal, bool success) {}

	// 拒一个本机型不支持的命令
	WashCarOperationResult rejectUnsupported(const WashCarOperation& operation,
	                                         ErrorCode::Code code,
	                                         const char* commandName);

	IDeviceService& deviceService;
	// 状态机。判定规则在对应机型的子类里(海天/驿公里各一套)。
	std::unique_ptr<WashCarStateMachine> m_stateMachine;
	// 日志里的机型名,如 "HaiTian" / "YiGongLi"
	std::string m_machineLabel;

	bool m_isMaintaining = false;
	bool m_isJogExecuting = false;
	bool m_statusMonitorRunning = false;
	// 上次已上报的故障表,状态没变就不重复发
	json m_lastFaultReport;
	// 上次已上报的变化快照,故障点位变化也会走到变化上报,内容没变就不重复发
	json m_lastValueChangeReport;

	MqttSender m_mqttSender;
	WashCarOperation m_monitorOperation;
	std::thread m_monitorThread;
	std::atomic<bool> m_monitorStopFlag{false};

	WashCarOperationResult m_currentStatus;

	// 点位表里某个服务的写点位(空串 = 点位表没配这个服务)
	std::string getServicePoint(const std::string& service,
	                            const std::string& selector = "default");

	// 状态机前置检查。返回 true = 拒绝，调用方不要再下发 PLC 写操作。
	bool checkStateMachine(WashCarCommand command, const char* commandName);

	// 互斥控制：检查是否可以执行云端命令（未被维护时返回true）
	bool canExecuteCloudCommand() const;

	// 点动执行：检查是否可以执行新的点动命令
	bool canExecuteJogCommand() const;

	// 点动执行：标记开始执行点动命令
	void setJogExecuting(bool executing);

	// 每日定时上报要报的点位(从洗车机点位表的 dailyReportSignals 取)。
	void collectDailyReportPoints(std::vector<std::pair<std::string, int>>& points);

	std::string getCurrentTimeStr();

	// 上报报文的公共字段与命名
	static std::int64_t getCurrentTimeSeconds();
	static std::string toFlexemPointName(const std::string& key);
	static void addCommonFlexemFields(json& msg, const std::string& boxNo);
};

class HaiTianWashCarService : public WashCarServiceBase {
public:
	HaiTianWashCarService(IDeviceService& deviceService);
	~HaiTianWashCarService();

	WashCarOperationResult startWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult stopWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult resetWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override;
	WashCarOperationResult clearAlarm(const WashCarOperation& operation) override;
	WashCarOperationResult pointReset(const WashCarOperation& operation) override;
	bool isWashing() const override;
	void startWashCarStatusMonitor(const WashCarOperation& operation, MqttSender sender) override;
	void executeDailyReportTask(MqttSender sender) override;
	void executeFaultReportTask(MqttSender sender) override;

protected:
	bool frontFxStatus(FxPlcStatus& out) override;

private:
	// 物料不足当天只上报一次
	bool m_m257ExceptionReportedToday = false;
	bool m_m259ExceptionReportedToday = false;
};

class YiGongLiWashCarService : public WashCarServiceBase {
public:
	YiGongLiWashCarService(IDeviceService& deviceService);
	~YiGongLiWashCarService();

	WashCarOperationResult startWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult stopWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult resetWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override;
	WashCarOperationResult clearAlarm(const WashCarOperation& operation) override;
	WashCarOperationResult pointReset(const WashCarOperation& operation) override;
	bool isWashing() const override;
	void startWashCarStatusMonitor(const WashCarOperation& operation, MqttSender sender) override;
	void executeDailyReportTask(MqttSender sender) override;
	void executeFaultReportTask(MqttSender sender) override;

	// 驿公里独有命令:实装 m206/m501/m510/m203
	WashCarOperationResult abortWashCar(const WashCarOperation& operation) override;
	WashCarOperationResult stopWork(const WashCarOperation& operation) override;
	WashCarOperationResult stopService(const WashCarOperation& operation) override;
	WashCarOperationResult resetSafe(const WashCarOperation& operation) override;

protected:
	bool frontFxStatus(FxPlcStatus& out) override;
	// pointOff 之后清自己的暂停标志
	void afterPointOff(const std::string& plcId, int pointDecimal, bool success) override;

private:
	// ===== 驿公里状态模型与命令确认 =====

	// 当前活跃 STL 步号(缓存 SMap 扫描,-1 = 缓存无 S 数据)
	int m_activeS = -1;
	// D1210 ASCII 状态字符串("stop"/"work"/"alarm"/"normal")
	std::string m_deviceStatusStr;
	// 最近 D1000 快照(PLC 断连时兜底判定用)
	int m_lastKnownD1000 = 0;
	// 暂停服务自身状态(断连时也拒绝启动类命令)
	bool m_isServicePaused = false;

	// 扫描缓存 SMap 找唯一 ON 的步号(找不到返回 -1)
	int scanActiveS(const FxPlcStatus& fx) const;
	// 解析缓存 D1210~D1212 为 ASCII 字符串(最多 6 字节,去尾随垃圾)
	std::string parseD1210(const FxPlcStatus& fx) const;
	// 轮询确认点:内部按 timing.washcarCommandConfirmTimeoutMs 超时,
	// 每 200ms 复用 viewAllDeviceStatus() 缓存(不额外发 MC 请求)
	bool waitForConfirm(const std::function<bool(const FxPlcStatus&)>& check);
	// 刷新最近状态快照(每次成功读回状态后调用)
	void updateLastKnownState(const FxPlcStatus& fx);
	// 从 viewAllDeviceStatus 缓存取驿公里 PLC 的 FxPlcStatus(取不到返回 false)
	bool getYiglFxStatus(FxPlcStatus& out);

	// 根据 op.mode 选 m521/m522/m523 之一(驿公里启动模式 1/2/3)
	// mode 取值: "1" / "2" / "3"
	int pickStartModeAddress(const WashCarOperation& operation) const;

	// 根据 op.mode 选 m202/m203 之一(驿公里复位 / 安全复位)
	// mode 取值: "" 普通复位 / "safe" 安全复位
	int pickResetModeAddress(const WashCarOperation& operation) const;

	// 通用 forceFxM 包装(填 boxNo/plcId/time/faults)
	bool writePointAndReport(const WashCarOperation& operation, int mAddr, bool value, int& outCode, std::string& outMessage);
};
