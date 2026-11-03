#include "business_layer/washcar/washcar_service.h"
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"   // [TEMP_DIAG] 阻塞点定位 trace
#include "common/time/time.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cctype>
#include <vector>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unistd.h>

#include <cstdint>

HaiTianWashCarService::HaiTianWashCarService(IDeviceService& deviceService, IReportSink& reportSink)
	: WashCarServiceBase(deviceService, reportSink, std::make_unique<HaiTianStateMachine>(), "HaiTian") {}

HaiTianWashCarService::~HaiTianWashCarService()
{

}

WashCarOperationResult HaiTianWashCarService::startWashCar(const WashCarOperation& operation)
{
	// 状态机前置检查：mode=log_only 只记日志照常执行，enforce 才真拒。
	if (checkStateMachine(WashCarCommand::START, "startWashCar")) {
		return m_currentStatus;
	}
	const long long startEnterMs = trace::nowMs();
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_START_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", mode=" + operation.getMode()
		+ ", plcId=" + operation.getPlcId()
		+ ", enterMs=" + std::to_string(startEnterMs));

	if (operation.getReqSource() == "mqtt" && !canExecuteCloudCommand()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_MAINTAINING);
		m_currentStatus.setMessage("maintaining");
		LOG_WARNING("[HaiTianWashCarService] startWashCar rejected: maintaining mode active, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	std::string plcId = operation.getPlcId();
	std::string mode = operation.getMode();

	PlcPointValues directStatus = deviceService.viewAllDeviceStatus();
	const PlcPointValues& fxList = directStatus;
    if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[HaiTianWashCarService] startWashCar rejected: no PLC available, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}
	auto fx = fxList;

	// 启动前置检查:4 条规则在 HaiTianStateMachine::checkStart 里,这里只写应答。
	{
		WashCarCommandResult startCheck;
		m_stateMachine->checkStart(fx, startCheck);
		if (!startCheck.allowed) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED);
			m_currentStatus.setMessage(startCheck.reason);
			LOG_WARNING("[HaiTianWashCarService] startWashCar rejected: " + startCheck.reason
				+ ", boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	}

    // 下发都设置为true。写点位来自洗车机点位表(见 default_point.cpp)。
	const long long startPlcEnterMs = trace::nowMs();
	const std::string startPoint = getServicePoint("start");
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_START_PLC_ENTER cmdId=") + trace::cmdId
		+ ", point=" + startPoint + ", val=true, enterMs=" + std::to_string(startPlcEnterMs));
    bool success = m_stateMachine->writePoint(deviceService, plcId, startPoint, true);
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_START_PLC_EXIT cmdId=") + trace::cmdId
		+ ", point=" + startPoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - startPlcEnterMs));
	if (success) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
		m_currentStatus.setMessage("success");
		m_currentStatus.setCurrentStep(0);
		LOG_INFO("[HaiTianWashCarService] startWashCar ok, write " + startPoint + "=true success, boxNo=" + operation.getBoxNo());
	} else {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED);
		m_currentStatus.setMessage("plc command failed");
		LOG_ERROR("[HaiTianWashCarService] startWashCar failed: write " + startPoint + "=true rejected by PLC, boxNo=" + operation.getBoxNo());
	}
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_START_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - startEnterMs));
	return m_currentStatus;
}

WashCarOperationResult HaiTianWashCarService::stopWashCar(const WashCarOperation& operation)
{
	// 停止任何状态都允许(第 6.1 条)，这里只打状态日志不拦。
	checkStateMachine(WashCarCommand::STOP, "stopWashCar");
    std::cout << "[HaiTianWashCarService] stopWashCar start" << std::endl;
	std::string plcId = operation.getPlcId();
    // [TEMP_DIAG] stopWashCar 入口时间戳, 用于算 stopWashCar 整体耗时
    const long long stopEnterMs = trace::nowMs();
    LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_STOP_ENTER cmdId=") + trace::cmdId
        + ", plcId=" + plcId
        + ", enterMs=" + std::to_string(stopEnterMs));
    LOG_INFO("[HaiTianWashCarService] stopWashCar start, plcId=" + plcId);
    // [TEMP_DIAG] stop 命令唯一的 PLC 写操作, wrap 它看耗时
    const long long stopPlcEnterMs = trace::nowMs();
    const std::string stopPoint = getServicePoint("stop");
    LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_STOP_PLC_ENTER cmdId=") + trace::cmdId
        + ", point=" + stopPoint + ", val=true, enterMs=" + std::to_string(stopPlcEnterMs));
    bool success = m_stateMachine->writePoint(deviceService, plcId, stopPoint, true);
    LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_STOP_PLC_EXIT cmdId=") + trace::cmdId
        + ", point=" + stopPoint + ", result=" + (success ? "ok" : "FAIL")
        + ", durationMs=" + std::to_string(trace::nowMs() - stopPlcEnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(success ? ErrorCode::Code::WASHCAR_STOPPED : ErrorCode::Code::WASHCAR_STOP_FAILED);
	m_currentStatus.setMessage(success ? "stopped" : "stop failed");

    std::cout << "[HaiTianWashCarService] stopWashCar finished" << std::endl;
    LOG_INFO("[HaiTianWashCarService] stopWashCar finished, plcId=" + operation.getPlcId() + ", success=" + (success ? "true" : "false"));
    // [TEMP_DIAG] stopWashCar 出口, 算 stopWashCar 整体耗时
    LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_STOP_EXIT cmdId=") + trace::cmdId
        + ", plcId=" + operation.getPlcId()
        + ", result=" + (success ? "ok" : "FAIL")
        + ", durationMs=" + std::to_string(trace::nowMs() - stopEnterMs));
	return m_currentStatus;
}

WashCarOperationResult HaiTianWashCarService::resetWashCar(const WashCarOperation& operation)
{
	// [TEMP_DIAG] resetWashCar 入口时间戳, 算整个函数耗时
	const long long resetEnterMs = trace::nowMs();
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", mode=" + operation.getMode()
		+ ", point=" + operation.getPoint()
		+ ", enterMs=" + std::to_string(resetEnterMs));

	if (operation.getReqSource() == "mqtt" && !canExecuteCloudCommand()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_MAINTAINING);
		m_currentStatus.setMessage("maintaining");
		LOG_WARNING("[HaiTianWashCarService] resetWashCar rejected: maintaining mode, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	std::string plcId = operation.getPlcId();
	std::string mode = operation.getMode();
	std::string point = operation.getPoint();

	PlcPointValues directStatus = deviceService.viewAllDeviceStatus();
	const PlcPointValues& fxList = directStatus;

	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[HaiTianWashCarService] resetWashCar rejected: no PLC, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	if (mode == "jog") {
		if (!canExecuteJogCommand()) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_JOG_EXECUTING);
		m_currentStatus.setMessage("jog executing");
			LOG_WARNING("[HaiTianWashCarService] resetWashCar jog rejected: another jog executing, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}

		if (point.empty()) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_JOG_POINT_REQUIRED);
		m_currentStatus.setMessage("point required for jog mode");
			LOG_WARNING("[HaiTianWashCarService] resetWashCar jog rejected: point missing, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}

		std::string pointUpper = point;
		for (auto& c : pointUpper) {
			c = std::toupper(c);
		}

		if (pointUpper.find("M") != 0) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_POINT_FORMAT);
		m_currentStatus.setMessage("invalid point format");
			LOG_WARNING(std::string("[HaiTianWashCarService] resetWashCar jog rejected: invalid point format, point=") + point);
			return m_currentStatus;
		}

		std::string addrStr = pointUpper.substr(1);
		try {
			int addr = std::stoi(addrStr);
			setJogExecuting(true);
			bool success = deviceService.forceFxM(plcId, addr, true);
			std::this_thread::sleep_for(std::chrono::seconds(1));
			bool successReset = deviceService.forceFxM(plcId, addr, false);
			setJogExecuting(false);
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED);
			m_currentStatus.setMessage(success ? "jog reset success" : "plc command failed");
			LOG_INFO(std::string("[HaiTianWashCarService] resetWashCar jog done, point=") + pointUpper
				+ ", on=" + (success ? "ok" : "FAIL")
				+ ", off=" + (successReset ? "ok" : "FAIL"));
			return m_currentStatus;
		}
		catch (const std::exception&) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_POINT_ADDRESS);
		m_currentStatus.setMessage("invalid point address");
			LOG_WARNING(std::string("[HaiTianWashCarService] resetWashCar jog rejected: invalid point address, point=") + point);
			return m_currentStatus;
		}
	}
	else if (mode != "auto" && !mode.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_MODE);
		m_currentStatus.setMessage("invalid mode");
		LOG_WARNING(std::string("[HaiTianWashCarService] resetWashCar rejected: invalid mode, mode=") + mode);
		return m_currentStatus;
	}

	auto fx = fxList;
	bool m0 = false;

	auto& status = fx;
	std::cout << "\n==================== PLC 点位表 ====================\n";
	for (const char type : {'Y', 'M', 'D', 'X', 'S'}) {
		std::cout << "----- " << type << " -----\n";
		for (const auto& [key, val] : status.values) {
			if (key.empty() || key[0] != type) {
				continue;
			}
			if (type == 'D') {
				std::cout << key << " = " << val << "\n";
			} else {
				std::cout << key << " = " << (val != 0 ? "ON" : "OFF") << "\n";
			}
		}
	}
	std::cout << "========================================================\n\n";

	if (!fx.getBit('M', 0, m0)) {
		std::cout << "read m0 failed" <<  std::endl;
		LOG_ERROR("[HaiTianWashCarService] resetWashCar: read M0 failed, plcId=" + plcId);
	}

    //if (m0 == 1) {
	// 复位是两步:先清停止标志(clearStop 服务,写 0),再写复位点位(reset 服务,写 1)。
	// 两个点位都来自洗车机点位表。
	const std::string clearPoint = getServicePoint("clearStop");
	const std::string resetPoint = getServicePoint("reset");
	const long long resetPlc1EnterMs = trace::nowMs();
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_PLC_ENTER cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + clearPoint + ", val=false, enterMs=" + std::to_string(resetPlc1EnterMs));
	if (!m_stateMachine->writePoint(deviceService, plcId, clearPoint, false)) {
		std::cout << "force " << clearPoint << " failed" << std::endl;
		LOG_ERROR("[HaiTianWashCarService] resetWashCar: write " + clearPoint + "=false failed, plcId=" + plcId);
	}
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_PLC_EXIT cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + clearPoint + ", val=false, durationMs=" + std::to_string(trace::nowMs() - resetPlc1EnterMs));
    //}

	sleep(1); // 等待1秒确保M0的状态被PLC处理

	// 下发都设置为true
	const long long resetPlc2EnterMs = trace::nowMs();
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_PLC_ENTER cmdId=") + trace::cmdId
		+ ", step=2/2, point=" + resetPoint + ", val=true, enterMs=" + std::to_string(resetPlc2EnterMs));
	bool success = m_stateMachine->writePoint(deviceService, plcId, resetPoint, true);
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_PLC_EXIT cmdId=") + trace::cmdId
		+ ", step=2/2, point=" + resetPoint + ", val=true, result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - resetPlc2EnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_RESET_FAILED);
	m_currentStatus.setMessage(success ? "reset success" : "reset failed");
	m_currentStatus.setCurrentStep(0);
	if (success) {
		LOG_INFO("[HaiTianWashCarService] resetWashCar ok, write " + resetPoint + "=true success, boxNo=" + operation.getBoxNo());
		// 复位下发成功 → 做安全自检(第 6.2 条)。部件回原位不是瞬间完成的，等一下再判。
		// 自检不通过或故障仍在 → 升级为严重故障，之后拒绝远程复位。
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		PlcPointValues fxAfterReset;
		if (frontPoints(fxAfterReset)) {
			m_stateMachine->noteResetConfirmed(fxAfterReset, m_isMaintaining);
		}
	} else {
		LOG_ERROR("[HaiTianWashCarService] resetWashCar failed: write " + resetPoint + "=true rejected, boxNo=" + operation.getBoxNo());
	}
	// [TEMP_DIAG] resetWashCar 出口, 算整个函数耗时
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_RESET_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - resetEnterMs));
	return m_currentStatus;
}

WashCarOperationResult HaiTianWashCarService::getWashCarStatus(const WashCarOperation& operation) {
	// [TEMP_DIAG] readData 入口 (跟 stop/reset 一致模式), viewAllDeviceStatus 内部是 cache 读,
	// 真正阻塞点在 fx_plc_instance.cpp sendAndReceive (cache miss 时)
	const long long readEnterMs = trace::nowMs();
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_READ_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", queryPoints=" + std::to_string(operation.getQueryPoints().size()));

	PlcPointValues directStatus = deviceService.viewAllDeviceStatus();
	const PlcPointValues& fxList = directStatus;

	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_READ_EXIT cmdId=") + trace::cmdId
			+ ", result=no_plc, durationMs=" + std::to_string(trace::nowMs() - readEnterMs));
		return m_currentStatus;
	}

	auto fx = fxList;
	m_currentStatus.clearStatusPoints();
	
	uint16_t d142 = 0;
    
    if (fx.getWord('D', 142, d142)) {
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
        m_currentStatus.setCurrentStep(d142);
        m_currentStatus.setMessage("D142=" + std::to_string(d142));
		std::cerr << "[HaiTianWashCarService] getWashCarStatus: d142=" << d142 << std::endl;
    }
    //这里替换成s0,getBit('S', 0,s0),s0=1是处于停止状态
    bool s0 = false;
    if (fx.getBit('S', 0, s0)) {
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode((s0 == 1) ? ErrorCode::Code::WASHCAR_RESET_FAILED : ErrorCode::Code::SUCCESS);
        m_currentStatus.setMessage(s0 ? "S0=1" : "S0=0");
        std::cerr << "[HaiTianWashCarService] getWashCarStatus: s0=" << (s0 ? "1" : "0") << std::endl;
    }
	/*bool hasFault = !checkCurrentAlarms();
	if (hasFault) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("fault exist");
	}*/
	
	const auto& queryPoints = operation.getQueryPoints();
	for (const auto& [key, _] : queryPoints) {
		if (key.size() < 2) {
			LOG_WARNING(std::string("[HaiTianWashCarService] skip invalid query point=") + key);
			continue;
		}

		try {
			if (key.substr(0, 1) == "d" || key.substr(0, 1) == "D") {
				int dNum = std::stoi(key.substr(1));
				uint16_t value = 0;
				if (fx.getWord('D', dNum, value)) {
					m_currentStatus.addStatusPoint(key, value);
				}
			} else if (key.substr(0, 1) == "y" || key.substr(0, 1) == "Y") {
				int yNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getBit('Y', yNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			} else if (key.substr(0, 1) == "m" || key.substr(0, 1) == "M") {
				int mNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getBit('M', mNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			} else if (key.substr(0, 1) == "x" || key.substr(0, 1) == "X") {
				int xNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getBit('X', xNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			}
		} catch (const std::exception& e) {
			LOG_WARNING(std::string("[HaiTianWashCarService] skip invalid query point=") + key + ", err=" + e.what());
		}
	}

	// [TEMP_DIAG] readData 出口
	LOG_INFO(std::string("[HaiTianWashCarService] WASHCAR_READ_EXIT cmdId=") + trace::cmdId
		+ ", result=ok, pointCount=" + std::to_string(m_currentStatus.getStatus().size())
		+ ", durationMs=" + std::to_string(trace::nowMs() - readEnterMs));

	// [2026-08-31] 末尾补全: getWashCarStatus 成功 path 必须 setBoxNo + setCode + setMessage
	// 之前如果 D142/S0 双读失败,return 空白 status,调用方会拿到 code=0 message="" 误判成功
	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
	m_currentStatus.setMessage("read ok");
	// 状态查询附上状态机结论(第 4 节「查询状态」行)：
	// 状态、最高等级、全部有效异常、是否允许启动、是否可远程处理、建议操作。
	{
		PlcPointValues fx;
		if (frontPoints(fx)) {
			const std::string detail = m_stateMachine->describeNow(
				fx, m_isMaintaining);
			m_currentStatus.setMessage(m_currentStatus.getMessage() + " | " + detail);
			LOG_INFO("[StateMachine] SM_QUERY " + detail);
		} else {
			m_currentStatus.setMessage(m_currentStatus.getMessage() + " | 状态机未判定(PLC 数据未就绪)");
		}
	}
	return m_currentStatus;
}

bool HaiTianWashCarService::isWashing() const {
    if (m_currentStatus.getCurrentStep() == 0) return true;
	return m_currentStatus.getCode() == ErrorCode::Code::SUCCESS && m_currentStatus.getCurrentStep() > 0 && m_currentStatus.getCurrentStep() < 10;
}

WashCarOperationResult HaiTianWashCarService::clearAlarm(const WashCarOperation& operation)
{
	// 清故障本质是复位，走同一套远程恢复限制。
	if (checkStateMachine(WashCarCommand::RESET, "clearAlarm")) {
		return m_currentStatus;
	}
	std::string plcId = operation.getPlcId();

	PlcPointValues directStatus = deviceService.viewAllDeviceStatus();
	const PlcPointValues& fxList = directStatus;
	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[HaiTianWashCarService] clearAlarm rejected: no PLC, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	auto fx = fxList;
	bool x44 = false;

	// 检查急停是否被按下（X44=1表示急停按下）
	if (fx.getBit('X', 44, x44)) {
		if (!x44) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_ESTOP_NOT_PRESSED);
		m_currentStatus.setMessage("estop not pressed");
			LOG_WARNING("[HaiTianWashCarService] clearAlarm rejected: estop not pressed (X44=0), boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	} else {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_FAILED);
		m_currentStatus.setMessage("read x44 failed");
		LOG_ERROR("[HaiTianWashCarService] clearAlarm rejected: read X44 failed, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

    // 在发复位指令(m100=1)前先读m0，如果m0=1,要先发m0=0
	// 急停已按下，执行复位流程（M100=1）
	bool m0Success = m_stateMachine->writePoint(deviceService, plcId, getServicePoint("clearStop"), false);
	bool m100Success = m_stateMachine->writePoint(deviceService, plcId, getServicePoint("reset"), true);

	if (m100Success) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
		m_currentStatus.setMessage("alarm cleared");
		LOG_INFO("[HaiTianWashCarService] clearAlarm ok, m0_off=" + std::string(m0Success ? "ok" : "FAIL")
			+ ", m100_on=ok, boxNo=" + operation.getBoxNo());
	} else {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_FAILED);
		m_currentStatus.setMessage("clear alarm failed");
		LOG_ERROR("[HaiTianWashCarService] clearAlarm failed: m100 rejected by PLC, boxNo=" + operation.getBoxNo());
	}
	
	return m_currentStatus;
}

WashCarOperationResult HaiTianWashCarService::pointReset(const WashCarOperation& operation) {
    std::string plcId = operation.getPlcId();

    //point reset: all 等同于 washcar reset
    if (operation.getPoint() == "all") {
        m_stateMachine->writePoint(deviceService, plcId, getServicePoint("clearStop"), false);
        bool success = m_stateMachine->writePoint(deviceService, plcId, getServicePoint("reset"), true);

        m_currentStatus.setBoxNo("all");
        m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_RESET_FAILED);
        m_currentStatus.setMessage(success ? "reset success" : "reset failed");
        LOG_INFO(std::string("[HaiTianWashCarService] pointReset all, plcId=") + plcId
            + ", result=" + (success ? "ok" : "FAIL"));
    } else {
        //point reset: m100 等待确认，暂时不实现
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_POINT_RESET_UNSUPPORTED);
		m_currentStatus.setMessage("point reset: implementation pending confirmation");
        LOG_WARNING(std::string("[HaiTianWashCarService] pointReset rejected: only 'all' supported, got point=") + operation.getPoint());
    }

    return m_currentStatus;
}

void HaiTianWashCarService::startWashCarStatusMonitor(const WashCarOperation& operation) {
    std::cout << "[HaiTianWashCarService] startWashCarStatusMonitor" << std::endl;
    LOG_INFO("[HaiTianWashCarService] startWashCarStatusMonitor, boxNo=" + operation.getBoxNo() + ", reqSource=" + operation.getReqSource());
    if (m_statusMonitorRunning) return;

    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }

    m_statusMonitorRunning = true;
    m_monitorOperation = operation;
    
    m_monitorStopFlag.store(false);

    m_monitorThread.start("washcar-monitor", [this]() {
        while (!m_monitorStopFlag.load()) {

            if (m_monitorStopFlag.load()) break;

            auto status = getWashCarStatus(m_monitorOperation);

            if (m_monitorOperation.getReqSource() == "mqtt") {
                json msg;
                msg["D142"] = status.getCurrentStep();
                addCommonFlexemFields(msg, m_monitorOperation.getBoxNo());

                std::cout << "[HaiTianWashCarService] WashCarStatusMonitor: D142 is " << status.getCurrentStep() << std::endl;
                LOG_DEBUG("[HaiTianWashCarService] D142=" + std::to_string(status.getCurrentStep()));

                sendReport(ReportKind::MONITOR, msg.dump());
            }

            if (!isWashing()) {
                std::cout << "[HaiTianWashCarService] WashCarStatusMonitor stopped: not washing (d142="
                          << status.getCurrentStep() << ")" << std::endl;
                LOG_INFO("[HaiTianWashCarService] WashCarStatusMonitor stopped: not washing, d142=" + std::to_string(status.getCurrentStep()));
                m_monitorStopFlag.store(true);
                m_statusMonitorRunning = false;
                // 不可以用stopWashCarStatusMonitor()，会导致自己等待自己结束，产生死锁
                return;
            } else if (status.isCompleted()) {
                std::cout << "[HaiTianWashCarService] WashCarStatusMonitor stopped: completed (d142="
                          << status.getCurrentStep() << ")" << std::endl;
                LOG_INFO("[HaiTianWashCarService] WashCarStatusMonitor stopped: completed, d142=" + std::to_string(status.getCurrentStep()));
                m_monitorStopFlag.store(true);
                m_statusMonitorRunning = false;
                return;
            } else if (status.hasFault()) {
                std::cout << "[HaiTianWashCarService] WashCarStatusMonitor stopped: fault detected" << std::endl;
                m_monitorStopFlag.store(true);
                m_statusMonitorRunning = false;
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(
                ConfigManager::instance().timing().washcarMonitorIntervalMs));
        }
    });
}

void HaiTianWashCarService::executeDailyReportTask() {
    LOG_DEBUG("[HaiTianWashCarService] executeDailyReportTask tick");

    // 只在目标分钟发。调度器已经卡过一道，这里再卡一次，防止被别处误调
    if (!washcar::timeutil::isAtMinute(ConfigManager::instance().timing().dailyReportHour,
                                       ConfigManager::instance().timing().dailyReportMinute)) {
        return;
    }

    {
        m_m257ExceptionReportedToday = false;
        m_m259ExceptionReportedToday = false;
		bool m_m0ExceptionReportedToday = false;// only for test

        std::vector<std::pair<std::string, int>> points;
        collectDailyReportPoints(points);

        WashCarOperation operation;
        for (const auto& [key, value] : points) {
            operation.addQueryPoint(key, value);
        }

        auto result = getWashCarStatus(operation);

        json msg;
        const auto& status = result.getStatus();

        bool hasExceptionReport = false;
        for (const auto& [key, val] : status) {
            msg[toFlexemPointName(key)] = val;
            if (key == "m257" && val == 1 && !m_m257ExceptionReportedToday) {
                hasExceptionReport = true;
                m_m257ExceptionReportedToday = true;
            } else if (key == "m259" && val == 1 && !m_m259ExceptionReportedToday) {
                hasExceptionReport = true;
                m_m259ExceptionReportedToday = true;
            }
            else if (key == "m0" && val == 1 && !m_m0ExceptionReportedToday) {
                hasExceptionReport = true;
                m_m0ExceptionReportedToday = true;
            }
        }

        if (hasExceptionReport) {
            addCommonFlexemFields(msg, operation.getBoxNo());

            std::cout << "[HaiTianWashCarService] Timer send DailyReport (Exception)" << std::endl;
            LOG_WARNING("[HaiTianWashCarService] DailyReport exception published, keys=" + std::to_string(msg.size()));
            sendReport(ReportKind::PUSHALARM, msg.dump());
        }
    }
}

void HaiTianWashCarService::executeFaultReportTask() {
    auto allStatus = deviceService.viewAllDeviceStatus();
    const PlcPointValues& fxList = allStatus;
    if (fxList.empty()) return;

    const auto& fx = fxList;
    // 故障报文由状态机组(海天稠密:事件类点位全带,读不到的按 0)
    json msg = m_stateMachine->buildFaultReport(fx);

    // 状态没变不发;变了才发并记下,等下次变化
    if (msg == m_lastFaultReport) return;
    m_lastFaultReport = msg;

    addCommonFlexemFields(msg, "");
    LOG_WARNING("[HaiTianWashCarService] FaultReport, payload=" + msg.dump());
    sendReport(ReportKind::PUSHALARM, msg.dump());
}

