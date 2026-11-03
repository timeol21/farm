#include "business_layer/washcar/washcar_service.h"
#include "common/topics.h"
#include "common/log/log_manager.h"

#include <iostream>
#include <ctime>
#include <cctype>
#include <cstring>
#include <vector>
#include <algorithm>

WashCarServiceBase::WashCarServiceBase(IDeviceService& deviceService,
                                       std::unique_ptr<WashCarStateMachine> stateMachine,
                                       std::string machineLabel)
    : deviceService(deviceService),
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

bool WashCarServiceBase::checkStateMachine(WashCarCommand command, const char* commandName) {
	FxPlcStatus fx;
	if (!frontFxStatus(fx)) return false;   // 没 PLC 数据就不判定
	const bool blocked = m_stateMachine->checkAndApply(
		toPlcPointValues(fx), m_isMaintaining, command, commandName);
	if (!blocked) return false;

	// 被拦时把状态机的理由和建议带给调用方。
	m_currentStatus.setCode(ErrorCode::Code::WASHCAR_START_REJECTED);
	return true;
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

bool WashCarServiceBase::changeMode(const WashCarOperation& operation) {
	std::string reqSource = operation.getReqSource();

	if (reqSource != "http") {
		return false;
	}

	m_isMaintaining = !m_isMaintaining;
	// 维修标志落盘：现场调试到一半断电，重启后还得是维修模式(第 6.5 条)。
	WashCarStateMachine::saveMaintenanceFlag(
		ConfigManager::instance().box().storageRoot, m_isMaintaining);
	LOG_WARNING("[" + m_machineLabel + "] 维修模式切换：maintaining=" + std::string(m_isMaintaining ? "1" : "0"));
	return true;
}

WashCarOperationResult WashCarServiceBase::pointOn(const WashCarOperation& operation) {
    if (checkStateMachine(WashCarCommand::POINT_ON, "pointOn")) {
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
    if (checkStateMachine(WashCarCommand::POINT_OFF, "pointOff")) {
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
    auto changedList = deviceService.getChangedFxPlcStatus();
    if (changedList.empty()) return;

    // 变化点摊成"点位 -> 值",报文怎么组由状态机决定(点位表说哪些是属性类)
    json pointObj;
    std::vector<std::string> plcIds;
    PlcPointValues changed;

    for (const auto& status : changedList) {
        plcIds.push_back(status.getDeviceId());
        const auto points = toPlcPointValues(status);
        changed.values.insert(points.values.begin(), points.values.end());
    }
    pointObj = m_stateMachine->buildValueChangeReport(changed);

    if (!pointObj.empty()) {
        json msg;
        msg.update(pointObj);

        // 状态没变不发;变了才发并记下,等下次变化
        const bool changed = (msg != m_lastValueChangeReport);
        m_lastValueChangeReport = msg;

        if (changed) {
            addCommonFlexemFields(msg, "");
            std::cout << "[" << m_machineLabel << "WashCarService] Timer send ValueChange" << std::endl;
            LOG_INFO("[" + m_machineLabel + "WashCarService] ValueChange report sent, pointCount=" + std::to_string(pointObj.size()));
            sender(WASHCAR_CHANGE_REPORT_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_VALUE_CHANGE);
        }
        deviceService.clearChangedFxPlcStatus(plcIds);
    }
}

void WashCarServiceBase::stopWashCarStatusMonitor() {
    std::cout << "[" << m_machineLabel << "WashCarService] stopWashCarStatusMonitor" << std::endl;
    LOG_INFO("[" + m_machineLabel + "WashCarService] stopWashCarStatusMonitor");
    m_monitorStopFlag.store(true);
    m_statusMonitorRunning = false;

    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
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
