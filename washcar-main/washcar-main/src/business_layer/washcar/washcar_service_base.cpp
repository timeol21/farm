#include "business_layer/washcar/washcar_service.h"
#include "common/log/log_manager.h"
#include "common/config/point.h"

#include <iostream>
#include <ctime>
#include <cctype>
#include <cstring>
#include <vector>
#include <algorithm>

static int pointValueOf(const FxPlcStatus& status, const std::string& point) {
    const auto parsed = washcar::cfg::parsePoint(point);
    if (parsed.type == 'D') {
        uint16_t value = 0;
        return status.getDRegister(parsed.addr, value) ? static_cast<int>(value) : 0;
    }
    bool value = false;
    switch (parsed.type) {
    case 'Y': return status.getYBit(parsed.addr, value) && value ? 1 : 0;
    case 'M': return status.getMBit(parsed.addr, value) && value ? 1 : 0;
    case 'X': return status.getXBit(parsed.addr, value) && value ? 1 : 0;
    case 'S': return status.getSBit(parsed.addr, value) && value ? 1 : 0;
    default:  return 0;
    }
}

WashCarServiceBase::WashCarServiceBase(IDeviceService& deviceService, ITimer& timer,
                                       std::unique_ptr<WashCarStateMachine> stateMachine,
                                       std::string machineLabel)
    : deviceService(deviceService),
      m_timer(timer),
      m_stateMachine(std::move(stateMachine)),
      m_machineLabel(std::move(machineLabel)) {
	// 维修标志从盒子本地恢复(第 6.5 条)。文件不存在按非维修处理。
	m_isMaintaining = WashCarStateMachine::loadMaintenanceFlag(
		ConfigManager::instance().box().storageRoot);
	if (m_isMaintaining) {
		LOG_WARNING("[" + m_machineLabel + "] 从本地存储恢复维修模式：maintaining=1");
	}
}

// 点位表里某个服务的写点位(空串 = 没配这个服务)。命令写哪个软元件由点位表决定。
std::string WashCarServiceBase::getServicePoint(const std::string& service,
                                                 const std::string& selector) {
    return m_stateMachine->servicePoint(service, selector);
}

std::vector<FxPlcStatus> WashCarServiceBase::viewPlcStatus() {
	return deviceService.viewAllDeviceStatus().getFxPlcStatusList();
}

WashCarAdmission WashCarServiceBase::admitCommand(WashCarCommand command, const char* commandName) {
	FxPlcStatus fx;
	if (!frontFxStatus(fx)) {
		// 没 PLC 数据就不判定(与改造前一致: 断连时不拦顾客命令)
		return WashCarAdmission{};
	}
	WashCarAdmission admission = m_stateMachine->admit(
		toPlcPointValues(fx), m_isMaintaining, command, commandName);
	if (admission.blocked) {
		// 被拦时把状态机的理由带给调用方, 云端失败回执直接用它
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED);
		// 理由 + 建议一起回给云端, 现场照着日志就能操作(如"请先下发复位")
		std::string text = admission.reason.empty() ? std::string("command rejected by state machine")
		                                            : admission.reason;
		if (!admission.suggestion.empty()) text += "；" + admission.suggestion;
		m_currentStatus.setMessage(text);
		LOG_WARNING(std::string("[") + m_machineLabel + "WashCarService] " + commandName
			+ " rejected by state machine: " + m_currentStatus.getMessage());
	}
	return admission;
}

void WashCarServiceBase::rollbackCommand(const WashCarAdmission& admission, const char* commandName) {
	m_stateMachine->rollback(admission, commandName);
}

bool WashCarServiceBase::canExecuteCloudCommand() const {
	return !m_isMaintaining;
}

bool WashCarServiceBase::canExecuteJogCommand() const {
	return !m_isJogExecuting;
}

void WashCarServiceBase::setJogExecuting(bool executing) {
	m_isJogExecuting = executing;
}

WashCarOperationResult WashCarServiceBase::maintain(const WashCarOperation& operation) {
	m_currentStatus.setBoxNo(operation.getBoxNo());

	// 维修是本地配置台的操作, 云端不能远程把机器切进维护
	if (operation.getReqSource() != "http") {
		m_currentStatus.setCode(ErrorCode::Code::AUTH_PERMISSION_DENIED);
		m_currentStatus.setMessage("maintenance command is local only");
		LOG_WARNING("[" + m_machineLabel + "] maintain rejected: only http source, reqSource=" + operation.getReqSource());
		return m_currentStatus;
	}

	const WashCarCommand command = m_isMaintaining ? WashCarCommand::EXIT_MAINTENANCE
	                                              : WashCarCommand::ENTER_MAINTENANCE;
	const WashCarAdmission admission = admitCommand(command, "maintain");
	if (admission.blocked) {
		return m_currentStatus;
	}

	m_isMaintaining = !m_isMaintaining;
	// 维修标志落盘：现场调试到一半断电，重启后还得是维修模式(第 6.5 条)。
	WashCarStateMachine::saveMaintenanceFlag(
		ConfigManager::instance().box().storageRoot, m_isMaintaining);
	m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
	m_currentStatus.setMessage(m_isMaintaining ? "maintenance on" : "maintenance off");
	LOG_WARNING("[" + m_machineLabel + "] 维修模式切换：maintaining=" + std::string(m_isMaintaining ? "1" : "0"));
	return m_currentStatus;
}

WashCarOperationResult WashCarServiceBase::pointOn(const WashCarOperation& operation) {
    if (admitCommand(WashCarCommand::POINT_ON, "pointOn").blocked) {
        return m_currentStatus;
    }
    std::string plcId = operation.getPlcId();
    int pointDecimal = operation.getPointDecimal();

    bool success = deviceService.forceFxM(plcId, pointDecimal, true);

    m_currentStatus.setBoxNo(operation.getBoxNo());
    m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_POINT_ON_FAILED);
    m_currentStatus.setMessage(success ? "point on success" : "point on failed");
    LOG_INFO(std::string("[") + m_machineLabel + "WashCarService] pointOn plcId=" + plcId + ", m=" + std::to_string(pointDecimal)
        + ", result=" + (success ? "ok" : "FAIL"));

    return m_currentStatus;
}

WashCarOperationResult WashCarServiceBase::pointOff(const WashCarOperation& operation) {
    if (admitCommand(WashCarCommand::POINT_OFF, "pointOff").blocked) {
        return m_currentStatus;
    }
    std::string plcId = operation.getPlcId();
    int pointDecimal = operation.getPointDecimal();

    bool success = deviceService.forceFxM(plcId, pointDecimal, false);
    afterPointOff(plcId, pointDecimal, success);

    m_currentStatus.setBoxNo(operation.getBoxNo());
    m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_POINT_OFF_FAILED);
    m_currentStatus.setMessage(success ? "point off success" : "point off failed");
    LOG_INFO(std::string("[") + m_machineLabel + "WashCarService] pointOff plcId=" + plcId + ", m=" + std::to_string(pointDecimal)
        + ", result=" + (success ? "ok" : "FAIL"));

    return m_currentStatus;
}

WashCarOperationResult WashCarServiceBase::pointStatus(const WashCarOperation& operation) {
    BoxDeviceStatus status = deviceService.viewAllDeviceStatus();
    const auto& fxList = status.getFxPlcStatusList();

    if (fxList.empty()) {
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
        LOG_WARNING("[" + m_machineLabel + "WashCarService] pointStatus rejected: no PLC");
        return m_currentStatus;
    }

    const auto& fx = fxList.front();

    if (operation.getPoint() == "all") {
        //返回所有点位
        for (const auto& [addr, val] : fx.getMMap()) {
            m_currentStatus.addStatusPoint("m" + std::to_string(addr), val ? 1 : 0);
        }
        for (const auto& [addr, val] : fx.getYMap()) {
            m_currentStatus.addStatusPoint("y" + std::to_string(addr), val ? 1 : 0);
        }
        for (const auto& [addr, val] : fx.getDMap()) {
            m_currentStatus.addStatusPoint("d" + std::to_string(addr), val);
        }
        for (const auto& [addr, val] : fx.getXMap()) {
            m_currentStatus.addStatusPoint("x" + std::to_string(addr), val ? 1 : 0);
        }
        for (const auto& [addr, val] : fx.getSMap()) {
            m_currentStatus.addStatusPoint("s" + std::to_string(addr), val ? 1 : 0);
        }
    } else {
        //返回指定点位
        std::string pointType = operation.getPointType();
        int pointDecimal = operation.getPointDecimal();

        if (pointType == "M") {
            bool val = false;
            if (fx.getMBit(pointDecimal, val)) {
                m_currentStatus.addStatusPoint(operation.getPoint(), val ? 1 : 0);
            }
        } else if (pointType == "Y") {
            bool val = false;
            if (fx.getYBit(pointDecimal, val)) {
                m_currentStatus.addStatusPoint(operation.getPoint(), val ? 1 : 0);
            }
        } else if (pointType == "D") {
            uint16_t val = 0;
            if (fx.getDRegister(pointDecimal, val)) {
                m_currentStatus.addStatusPoint(operation.getPoint(), val);
            }
        } else if (pointType == "X") {
            bool val = false;
            if (fx.getXBit(pointDecimal, val)) {
                m_currentStatus.addStatusPoint(operation.getPoint(), val ? 1 : 0);
            }
        }
    }

    m_currentStatus.setBoxNo(operation.getBoxNo());
    m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
    m_currentStatus.setMessage("success");
    return m_currentStatus;
}

void WashCarServiceBase::executeValueChangeTask(MqttSender sender) {
    const auto changedPlcList = deviceService.getChangedFxPlcStatus();
    if (changedPlcList.empty()) return;

    // 变化点摊成"点位 -> 值",报文怎么组由状态机决定(点位表说哪些是属性类)
    json pointValues = json::object();
    std::vector<std::string> changedPlcIds;//待清点位变化标志的PLC的id

    for (const auto& plcStatus : changedPlcList) {
        changedPlcIds.push_back(plcStatus.getDeviceId());
        // 只把本轮真正变化的点交给状态机;同一个点没变就不再重复上报。
        // 不用兜"没有变化点就全量上报"的分支: 缓存里 isChanged() 为真的 PLC
        // 一定带变化点(changed_ 只跟 markPointChanged 一起置位, clearChanged 一起清)。
        PlcPointValues plcChangedPoints;
        plcChangedPoints.plcId = plcStatus.getDeviceId();
        for (const auto& point : plcStatus.getChangedPoints()) {
            plcChangedPoints.values[point] = pointValueOf(plcStatus, point);
        }
        // 每台 PLC 组一份报文(状态机顺带打关键点位变化日志), 再合并成一份。
        // 合并时先到的赢 —— 跟原来把多台 PLC 的点位并成一张表再组报文是一个语义。
        const json plcReport = m_stateMachine->buildValueChangeReport(plcChangedPoints);
        for (const auto& [key, value] : plcReport.items()) {
            if (!pointValues.contains(key)) pointValues[key] = value;
        }
    }

    if (!pointValues.empty()) {
        json msg;
        msg.update(pointValues);

        // 状态没变不发;变了才发并记下,等下次变化
        const bool reportChanged = (msg != m_lastValueChangeReport);
        m_lastValueChangeReport = msg;

        if (reportChanged) {
            addCommonFlexemFields(msg, "");
            std::cout << "[" << m_machineLabel << "WashCarService] Timer send ValueChange" << std::endl;
            LOG_INFO("[" + m_machineLabel + "WashCarService] ValueChange report sent, pointCount=" + std::to_string(pointValues.size()));
            sender(Command::Endpoint::MONITOR, msg.dump(), MessageBuffer::MessageType::TYPE_VALUE_CHANGE);
        }
        deviceService.clearChangedFxPlcStatus(changedPlcIds);
    }
}

void WashCarServiceBase::stopWashCarStatusMonitor() {
    LOG_INFO("[" + m_machineLabel + "WashCarService] stopWashCarStatusMonitor");
    m_statusMonitorRunning = false;
    // 线程归定时器: 从监控任务自己的线程里调时会延后 join(Timer 内部处理)
    m_timer.removeTask("washcar_monitor");
}

// 本机型不支持的命令。驿公里覆盖对应的几个。
WashCarOperationResult WashCarServiceBase::rejectUnsupported(const WashCarOperation& operation,
                                                            ErrorCode::Code code,
                                                            const char* commandName) {
	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(code);
	m_currentStatus.setMessage(std::string(commandName) + " unsupported by " + m_machineLabel);
	LOG_WARNING("[" + m_machineLabel + "WashCarService] " + commandName + " rejected: unsupported, boxNo=" + operation.getBoxNo());
	return m_currentStatus;
}

WashCarOperationResult WashCarServiceBase::abortWashCar(const WashCarOperation& operation) {
	return rejectUnsupported(operation, ErrorCode::Code::WASHCAR_ABORT_FAILED, "abortWashCar");
}

WashCarOperationResult WashCarServiceBase::stopWork(const WashCarOperation& operation) {
	return rejectUnsupported(operation, ErrorCode::Code::WASHCAR_STOP_WORK_FAILED, "stopWork");
}

WashCarOperationResult WashCarServiceBase::stopService(const WashCarOperation& operation) {
	return rejectUnsupported(operation, ErrorCode::Code::WASHCAR_STOP_SERVICE_FAILED, "stopService");
}

WashCarOperationResult WashCarServiceBase::resetSafe(const WashCarOperation& operation) {
	return rejectUnsupported(operation, ErrorCode::Code::WASHCAR_RESET_SAFE_FAILED, "resetSafe");
}

void WashCarServiceBase::collectDailyReportPoints(std::vector<std::pair<std::string, int>>& points) {
    // 每日定时上报的点位由洗车机点位表决定(dailyReportSignals)。
    const auto& pointTable = ConfigManager::instance().machine();
    for (const auto& name : pointTable.dailyReportSignals) {
        const std::string point = m_stateMachine->signalPoint(name);
        if (point.empty()) continue;
        // point 是 "M257",拆出 prefix + addr
        std::string prefix(1, static_cast<char>(std::tolower(static_cast<unsigned char>(point[0]))));
        points.push_back({prefix + point.substr(1), 0});
    }
}

std::string WashCarServiceBase::getCurrentTimeStr() {
    time_t now = time(nullptr);
    tm* tm_now = localtime(&now);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_now->tm_year + 1900,
             tm_now->tm_mon + 1,
             tm_now->tm_mday,
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec);
    return std::string(buf);
}

std::int64_t WashCarServiceBase::getCurrentTimeSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string WashCarServiceBase::toFlexemPointName(const std::string& key) {
    std::string result = key;
    for (auto& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

void WashCarServiceBase::addCommonFlexemFields(json& msg, const std::string& boxNo) {
    // [2026-08-26] 优先用调用方传的 boxNo;空时从 ConfigManager 读真实值(不再 hardcoded "123456")
    // 之前 executeValueChangeTask / executeFaultReportTask 传 "" 走默认值,变化/故障上报 boxNo 永远 = "123456",跟实际 boxNo 不符
    msg["boxNo"] = boxNo.empty() ? ConfigManager::instance().box().boxNo : boxNo;
    msg["time"] = getCurrentTimeSeconds();
}
