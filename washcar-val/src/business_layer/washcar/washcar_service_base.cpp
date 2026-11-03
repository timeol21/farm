#include "business_layer/washcar/washcar_service.h"
#include "common/config/point.h"
#include "common/log/log_manager.h"
#include "common/time/time.h"

#include <iostream>
#include <cctype>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {

/// 上报报文里的点位名是小写（"m187"），跟云端 Excel 的历史口径一致
std::string toLowerKey(const std::string& key) {
    std::string result = key;
    for (auto& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

}  // namespace

WashCarServiceBase::WashCarServiceBase(IDeviceService& deviceService,
                                       IReportSink& reportSink,
                                       std::unique_ptr<WashCarStateMachine> stateMachine,
                                       std::string machineLabel)
    : deviceService(deviceService),
      reportSink(reportSink),
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

bool WashCarServiceBase::frontPoints(PlcPointValues& out) {
	out = deviceService.viewAllDeviceStatus();
	return !out.empty();
}

bool WashCarServiceBase::checkStateMachine(WashCarCommand command, const char* commandName) {
	PlcPointValues points;
	if (!frontPoints(points)) return false;   // 没 PLC 数据就不判定
	const bool blocked = m_stateMachine->checkAndApply(points, m_isMaintaining, command, commandName);
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
    const PlcPointValues fx = deviceService.viewAllDeviceStatus();

    if (fx.empty()) {
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
        LOG_WARNING("[" + m_machineLabel + "WashCarService] pointStatus rejected: no PLC");
        return m_currentStatus;
    }

    if (operation.getPoint() == "all") {
        //返回所有点位（键沿用云端历史口径的小写）
        for (const auto& [key, val] : fx.values) {
            m_currentStatus.addStatusPoint(toLowerKey(key), val);
        }
    } else {
        //返回指定点位
        const auto point = washcar::cfg::parsePoint(operation.getPoint());
        if (point.type == '?') {
            m_currentStatus.setBoxNo(operation.getBoxNo());
            m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_POINT_FORMAT);
            m_currentStatus.setMessage("invalid point");
            return m_currentStatus;
        }
        const std::string key = washcar::cfg::formatPoint(point);
        int value = 0;
        if (fx.getWord(key, value)) {
            m_currentStatus.addStatusPoint(operation.getPoint(), value);
        }
    }

    m_currentStatus.setBoxNo(operation.getBoxNo());
    m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
    m_currentStatus.setMessage("success");
    return m_currentStatus;
}

// ===== 点位采样（readdata 的数据来源）=====
// 只做"软元件名 -> 当前值"的映射，不掺业务语义：
//   D 寄存器回 16 位整数，M/Y/X/S 回 0/1。
// 采样走 PLC 状态缓存（frontFxStatus），不额外发 PLC 请求。
WashCarOperationResult WashCarServiceBase::readPoints(const std::vector<std::string>& points) {
    m_currentStatus.clearStatusPoints();
    m_currentStatus.setBoxNo(ConfigManager::instance().box().boxNo);

    PlcPointValues fx;
    if (!frontPoints(fx)) {
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
        m_currentStatus.setMessage("no plc");
        LOG_WARNING("[" + m_machineLabel + "WashCarService] readPoints rejected: no PLC data");
        return m_currentStatus;
    }

    std::vector<std::string> targets = points;
    if (targets.empty()) {
        targets = collectConfiguredPoints();
    }

    int readOk = 0;
    int skipped = 0;
    for (const auto& raw : targets) {
        const auto point = washcar::cfg::parsePoint(raw);
        if (point.type == '?') {
            ++skipped;
            continue;
        }
        const std::string key = washcar::cfg::formatPoint(point);

        bool ok = false;
        int value = 0;
        if (point.type == 'D') {
            ok = fx.getWord(key, value);
        } else {
            bool bit = false;
            ok = fx.getBit(key, bit);
            value = bit ? 1 : 0;
        }

        if (ok) {
            m_currentStatus.addStatusPoint(key, value);
            ++readOk;
        } else {
            // 采不到就不放进报文：宁可少一个点，也不要用 0 冒充真实值
            ++skipped;
            LOG_DEBUG("[" + m_machineLabel + "WashCarService] readPoints: point not in cache, key=" + key);
        }
    }

    m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
    m_currentStatus.setMessage("read points ok");
    LOG_INFO("[" + m_machineLabel + "WashCarService] readPoints done, requested="
             + std::to_string(targets.size()) + ", ok=" + std::to_string(readOk)
             + ", skipped=" + std::to_string(skipped));
    return m_currentStatus;
}

std::vector<std::string> WashCarServiceBase::collectConfiguredPoints() {
    const auto& acq = ConfigManager::instance().machine().acquisition;
    std::vector<std::string> all;
    all.reserve(acq.dPoints.size() + acq.mPoints.size() + acq.yPoints.size()
                + acq.xPoints.size() + acq.sPoints.size());
    for (const auto& p : acq.dPoints) all.push_back(p);
    for (const auto& p : acq.mPoints) all.push_back(p);
    for (const auto& p : acq.yPoints) all.push_back(p);
    for (const auto& p : acq.xPoints) all.push_back(p);
    for (const auto& p : acq.sPoints) all.push_back(p);
    return all;
}

void WashCarServiceBase::executeValueChangeTask() {
    auto changedList = deviceService.getChanged();
    if (changedList.empty()) return;

    // 变化点摊成"点位 -> 值",报文怎么组由状态机决定(点位表说哪些是属性类)
    std::vector<std::string> plcIds;
    PlcPointValues changed;

    for (const auto& [plcId, points] : changedList) {
        plcIds.push_back(plcId);
        changed.values.insert(points.values.begin(), points.values.end());
    }
    const json pointObj = m_stateMachine->buildValueChangeReport(changed);

    if (!pointObj.empty()) {
        json msg;
        msg.update(pointObj);

        // 状态没变不发;变了才发并记下,等下次变化
        const bool differs = (msg != m_lastValueChangeReport);
        m_lastValueChangeReport = msg;

        if (differs) {
            addCommonFlexemFields(msg, "");
            LOG_INFO("[" + m_machineLabel + "WashCarService] ValueChange report sent, pointCount="
                     + std::to_string(pointObj.size()));
            sendReport(ReportKind::MONITOR, msg.dump());
        }
        deviceService.clearChanged(plcIds);
    }
}

void WashCarServiceBase::sendReport(ReportKind kind, const std::string& payload) {
    reportSink.push(Report{kind, payload});
}

void WashCarServiceBase::stopWashCarStatusMonitor() {
    // 可能被调多次（BusinessLayer::stop + 析构），线程已经 join 过就直接返回，
    // 否则日志会重复三遍
    if (!m_monitorThread.joinable()) {
        return;
    }
    std::cout << "[" << m_machineLabel << "WashCarService] stopWashCarStatusMonitor" << std::endl;
    LOG_INFO("[" + m_machineLabel + "WashCarService] stopWashCarStatusMonitor");
    m_monitorStopFlag.store(true);
    m_statusMonitorRunning = false;

    m_monitorThread.join();
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

std::string WashCarServiceBase::toFlexemPointName(const std::string& key) {
    std::string result = key;
    for (auto& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

void WashCarServiceBase::addCommonFlexemFields(json& msg, const std::string& boxNo) {
    // 优先用调用方传的 boxNo;空时从 ConfigManager 读真实值
    msg["boxNo"] = boxNo.empty() ? ConfigManager::instance().box().boxNo : boxNo;
    msg["time"] = washcar::timeutil::nowSeconds();
}
