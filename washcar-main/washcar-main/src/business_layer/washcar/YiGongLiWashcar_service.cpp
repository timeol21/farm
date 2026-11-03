#include "business_layer/washcar/washcar_service.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"   // [TEMP_DIAG] 阻塞点定位 trace
#include <iostream>
#include <thread>
#include <chrono>
#include <cctype>
#include <vector>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>
#include <cstdint>

// 驿公里 washcar 协议关键 M 点位(参考 docs/驿公里_MQTT_协议对接总结.md)
namespace yigl {
    // 停止营业 m501
    static constexpr int M_STOP_WORK = 501;
    // 停止营业 2 m510(同时是暂停服务标志)
    static constexpr int M_STOP_SERVICE = 510;
    // 监控(M 点): m147/m148
    static constexpr int M_UNSAFE = 147;
    static constexpr int M_HARDWARE_FAULT = 148;
    // 设备状态 d1210 / 故障码 d1000
    static constexpr int D_DEVICE_STATUS = 1210;
    static constexpr int D_FAULT_CODE = 1000;
    // 暂停服务 LED(停止营业2屏显;M510 置位后短暂脉冲,非持续确认位)
    static constexpr int M_PAUSE_LED = 270;
    // STL 洗车流程步区间(S20~S81, S81=完成)
    static constexpr int S_WASH_FIRST = 20;
    static constexpr int S_WASH_LAST = 81;
}

YiGongLiWashCarService::YiGongLiWashCarService(IDeviceService& deviceService, ITimer& timer)
	: WashCarServiceBase(deviceService, timer, std::make_unique<YiGongLiStateMachine>(), "YiGongLi") {}

bool YiGongLiWashCarService::frontFxStatus(FxPlcStatus& out) {
	return getYiglFxStatus(out);
}

void YiGongLiWashCarService::afterPointOff(const std::string& plcId, int pointDecimal, bool success) {
    // 恢复暂停服务: M510 置 0 → 清除自身暂停标志
    if (success && pointDecimal == yigl::M_STOP_SERVICE && m_isServicePaused) {
        m_isServicePaused = false;
        LOG_INFO("[YiGongLiWashCarService] pointOff M510=0, service resumed (self state cleared), plcId=" + plcId);
    }
}

YiGongLiWashCarService::~YiGongLiWashCarService() {

}

bool YiGongLiWashCarService::getYiglFxStatus(FxPlcStatus& out) {
    BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
    const auto& fxList = directStatus.getFxPlcStatusList();
    if (fxList.empty()) return false;
    out = fxList.front();
    return true;
}

int YiGongLiWashCarService::scanActiveS(const FxPlcStatus& fx) const {
    // STL 步任意时刻唯一 ON;遍历缓存 SMap 找第一个 ON(找不到返回 -1)
    for (const auto& [addr, val] : fx.getSMap()) {
        if (val) return addr;
    }
    return -1;
}

std::string YiGongLiWashCarService::parseD1210(const FxPlcStatus& fx) const {
    // D1210~D1212 存 ASCII 字符串(三菱 $MOV,每个 D 高字节在前存 2 字符,最长 6 字节)
    // 取值: "stop"(4) / "work"(4) / "alarm"(5) / "normal"(6)
    std::string raw;
    for (int d = 0; d < 3; ++d) {
        uint16_t v = 0;
        if (!fx.getDRegister(yigl::D_DEVICE_STATUS + d, v)) break;
        char hi = static_cast<char>((v >> 8) & 0xFF);
        char lo = static_cast<char>(v & 0xFF);
        if (hi >= 0x20 && hi <= 0x7E) raw.push_back(hi);
        if (lo >= 0x20 && lo <= 0x7E) raw.push_back(lo);
    }
    // 去掉尾随垃圾(超长/非字母)
    while (!raw.empty() && !std::isalpha(static_cast<unsigned char>(raw.back()))) {
        raw.pop_back();
    }
    return raw;
}

bool YiGongLiWashCarService::waitForConfirm(const std::function<bool(const FxPlcStatus&)>& check) {
    // 复用 viewAllDeviceStatus 缓存(500ms/1s 已更新),不额外发 MC 请求
    // 每 200ms 轮询一次,总超时 = timing.washcarCommandConfirmTimeoutMs(默认 3000ms)
    int timeoutMs = ConfigManager::instance().timing().washcarCommandConfirmTimeoutMs;
    if (timeoutMs <= 0) timeoutMs = 3000;
    int waited = 0;
    while (waited < timeoutMs) {
        FxPlcStatus fx;
        if (getYiglFxStatus(fx)) {
            if (check(fx)) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        waited += 200;
    }
    return false;
}

void YiGongLiWashCarService::updateLastKnownState(const FxPlcStatus& fx) {
    m_activeS = scanActiveS(fx);
    uint16_t d1000 = 0;
    if (fx.getDRegister(yigl::D_FAULT_CODE, d1000)) m_lastKnownD1000 = d1000;
    m_deviceStatusStr = parseD1210(fx);
    bool m510 = false;
    if (fx.getMBit(yigl::M_STOP_SERVICE, m510) && !m510) {
        m_isServicePaused = false;   // PLC 侧已退出暂停,自身状态跟随
    }
}

// ===== 命令生命周期终结判定: 断连时用最近快照兜底 =====

WashCarOperationResult YiGongLiWashCarService::startWashCar(const WashCarOperation& operation)
{
	const long long startEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", mode=" + operation.getMode()
		+ ", plcId=" + operation.getPlcId()
		+ ", enterMs=" + std::to_string(startEnterMs));

	if (operation.getReqSource() == "mqtt" && !canExecuteCloudCommand()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_MAINTAINING);
		m_currentStatus.setMessage("maintaining");
		LOG_WARNING("[YiGongLiWashCarService] startWashCar rejected: maintaining mode active, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	// 驿公里协议: 启动洗车前必须携带明确的洗车模式 (m521/m522/m523 之一)
	// mode 字段空 / 非 "1"/"2"/"3" 都拒绝,避免 PLC 误启动默认模式 1
	const std::string& mode = operation.getMode();
	if (mode != "1" && mode != "2" && mode != "3") {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_MODE);
		m_currentStatus.setMessage("no mode");
		LOG_WARNING(std::string("[YiGongLiWashCarService] startWashCar rejected: mode missing or invalid, boxNo=")
			+ operation.getBoxNo() + ", mode=\"" + mode + "\" (expect 1/2/3)");
		return m_currentStatus;
	}

	std::string plcId = operation.getPlcId();

	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();
	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[YiGongLiWashCarService] startWashCar rejected: no PLC available, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}
	auto fx = fxList.front();
	// 先按当前读回的状态刷新自身快照:PLC 侧已退出暂停时,自身的暂停标志要跟着清,
	// 否则 PLC 都恢复到就绪了,服务还记着"暂停",启动会被误拒。
	updateLastKnownState(fx);

	//// === 驿公里启动前置检查 ===
	//// 用采集缓存(500ms/1s)读回状态,区分启动被拒的具体原因
	{
		// 自身暂停服务状态(PLC 断连时也能拒绝)。这是服务自身的运行状态,不是点位。
		if (m_isServicePaused) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_START_REJECTED);
			m_currentStatus.setMessage("paused service");
			LOG_WARNING("[YiGongLiWashCarService] startWashCar rejected: paused service (self state), boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
		// 点位相关的 3 条在 YiGongLiStateMachine::checkStart 里,这里只负责写应答。
		{
			WashCarCheckResult startCheck;
			m_stateMachine->checkStart(toPlcPointValues(fx), startCheck);
			if (!startCheck.allowed) {
				m_currentStatus.setBoxNo(operation.getBoxNo());
				m_currentStatus.setCode(ErrorCode::Code::WASHCAR_START_REJECTED);
				m_currentStatus.setMessage(startCheck.reason);
				LOG_WARNING("[YiGongLiWashCarService] startWashCar rejected: " + startCheck.reason
					+ ", boxNo=" + operation.getBoxNo());
				return m_currentStatus;
			}
		}
		updateLastKnownState(fx);
	}

	// 三态准入 + 占位迁移(防连点; 停止后必须先复位)
	const WashCarAdmission startAdmission = admitCommand(WashCarCommand::START, "startWashCar");
	if (startAdmission.blocked) {
		return m_currentStatus;
	}

	// 驿公里 PLC 文档规定:启动洗车需要"先下 m204=1 (启动准备),等 20ms,再下 m521/m522/m523 (选模式启动)"
	// 漏了 m204 或没 20ms 间隔,PLC 不会启动
	const long long startPlcEnterMs = trace::nowMs();

	// 步骤 1:下发启动准备点位(点位表 startPrepare 服务)
	const std::string preparePoint = getServicePoint("startPrepare");
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_PLC_ENTER cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + preparePoint + ", val=true, enterMs=" + std::to_string(startPlcEnterMs));
	bool successPrepare = m_stateMachine->writePoint(deviceService, plcId, preparePoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_PLC_EXIT cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + preparePoint + ", result=" + (successPrepare ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - startPlcEnterMs));
	if (!successPrepare) {
		rollbackCommand(startAdmission, "startWashCar");
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_START_PREPARE_FAILED);
		m_currentStatus.setMessage("start prepare (" + preparePoint + ") failed");
		LOG_ERROR("[YiGongLiWashCarService] startWashCar failed: write " + preparePoint + "=true rejected by PLC, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	// 步骤 2:等 200ms (PLC 处理 m204 准备)
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// 步骤 3:按 op.mode("1"/"2"/"3") 选启动点位(点位表 start 服务,选择条件 = mode)
	// 入口已校验 mode ∈ {"1","2","3"}
	const std::string startPoint = getServicePoint("start", mode);
	const long long startPlc2EnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_PLC_ENTER cmdId=") + trace::cmdId
		+ ", step=2/2, point=" + startPoint + ", val=true, enterMs=" + std::to_string(startPlc2EnterMs));
	bool success = m_stateMachine->writePoint(deviceService, plcId, startPoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_PLC_EXIT cmdId=") + trace::cmdId
		+ ", step=2/2, point=" + startPoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - startPlc2EnterMs));
	if (success) {
		// 写成功 ≠ 已启动: 轮询确认点 S20 ON
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool s20 = false;
			return fx.getSBit(20, s20) && s20;
		});
		m_currentStatus.setBoxNo(operation.getBoxNo());
		if (confirmed) {
			m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
			m_currentStatus.setMessage("success");
			m_currentStatus.setCurrentStep(1);
			LOG_INFO("[YiGongLiWashCarService] startWashCar ok (confirmed S20 ON), mode=" + mode
				+ ", write " + preparePoint + "=true + write " + startPoint + "=true success, boxNo=" + operation.getBoxNo());
		} else {
			// 超时未确认: 按最后读回状态定位原因
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
			m_currentStatus.setMessage("start not confirmed within " + std::to_string(ConfigManager::instance().timing().washcarCommandConfirmTimeoutMs) + "ms");
			LOG_WARNING("[YiGongLiWashCarService] startWashCar timeout: " + preparePoint + "+" + startPoint
				+ " written but S20 not ON within timeout, boxNo=" + operation.getBoxNo());
		}
	} else {
		rollbackCommand(startAdmission, "startWashCar");
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED);
		m_currentStatus.setMessage("plc command failed");
		LOG_ERROR("[YiGongLiWashCarService] startWashCar failed: write " + startPoint
			+ "=true rejected by PLC, boxNo=" + operation.getBoxNo());
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_START_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - startEnterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::stopWashCar(const WashCarOperation& operation)
{
    std::cout << "[YiGongLiWashCarService] stopWashCar start" << std::endl;
	std::string plcId = operation.getPlcId();
    // [TEMP_DIAG] stopWashCar 入口时间戳, 用于算 stopWashCar 整体耗时
    const long long stopEnterMs = trace::nowMs();
    LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_ENTER cmdId=") + trace::cmdId
        + ", plcId=" + plcId
        + ", enterMs=" + std::to_string(stopEnterMs));
    LOG_INFO("[YiGongLiWashCarService] stopWashCar start, plcId=" + plcId);

    // 先读回当前状态,判定"停止是否有效"
    FxPlcStatus fx;
    if (getYiglFxStatus(fx)) {
        int activeS = scanActiveS(fx);
        updateLastKnownState(fx);
        // 洗车中(S20~S81): PLC 完全不响应 M205,回执"停止无效"
        if (activeS >= yigl::S_WASH_FIRST && activeS <= yigl::S_WASH_LAST) {
            m_currentStatus.setBoxNo(operation.getBoxNo());
            m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STOP_INVALID_WHILE_WASHING);
            m_currentStatus.setMessage("stop ignored while washing (S" + std::to_string(activeS) + "), use abort or estop");
            LOG_WARNING("[YiGongLiWashCarService] stopWashCar invalid: washing activeS=" + std::to_string(activeS)
                + ", PLC ignores M205, boxNo=" + operation.getBoxNo());
            return m_currentStatus;
        }
        // 归位中(S1/S2): 停止需同时按急停 X36,回执"请按急停"
        if (activeS == 1 || activeS == 2) {
            m_currentStatus.setBoxNo(operation.getBoxNo());
            m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STOP_INVALID_WHILE_WASHING);
            m_currentStatus.setMessage("homing in progress, press estop to stop (S" + std::to_string(activeS) + ")");
            LOG_WARNING("[YiGongLiWashCarService] stopWashCar invalid: homing activeS=" + std::to_string(activeS)
                + ", need estop, boxNo=" + operation.getBoxNo());
            return m_currentStatus;
        }
    }

    const WashCarAdmission stopAdmission = admitCommand(WashCarCommand::STOP, "stopWashCar");
    if (stopAdmission.blocked) {
        return m_currentStatus;
    }

    // [TEMP_DIAG] stop 命令唯一的 PLC 写操作, wrap 它看耗时
    const long long stopPlcEnterMs = trace::nowMs();
    const std::string stopPoint = getServicePoint("stop");
    LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_PLC_ENTER cmdId=") + trace::cmdId
        + ", point=" + stopPoint + ", val=true, enterMs=" + std::to_string(stopPlcEnterMs));
    bool success = m_stateMachine->writePoint(deviceService, plcId, stopPoint, true);
    LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_PLC_EXIT cmdId=") + trace::cmdId
        + ", point=" + stopPoint + ", result=" + (success ? "ok" : "FAIL")
        + ", durationMs=" + std::to_string(trace::nowMs() - stopPlcEnterMs));

    if (success) {
        // 写成功 → 轮询确认点 S0 ON
        bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
            bool s0 = false;
            return fx.getSBit(0, s0) && s0;
        });
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(confirmed ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
        m_currentStatus.setMessage(confirmed ? "stopped" : "stop not confirmed within timeout");
        if (!confirmed) {
            LOG_WARNING("[YiGongLiWashCarService] stopWashCar timeout: M205 written but S0 not ON, plcId=" + operation.getPlcId());
        }
    } else {
        rollbackCommand(stopAdmission, "stopWashCar");
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STOP_FAILED);
        m_currentStatus.setMessage("stop failed");
    }

    std::cout << "[YiGongLiWashCarService] stopWashCar finished" << std::endl;
    LOG_INFO("[YiGongLiWashCarService] stopWashCar finished, plcId=" + operation.getPlcId() + ", success=" + (success ? "true" : "false"));
    // [TEMP_DIAG] stopWashCar 出口, 算 stopWashCar 整体耗时
    LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_EXIT cmdId=") + trace::cmdId
        + ", plcId=" + operation.getPlcId()
        + ", result=" + (success ? "ok" : "FAIL")
        + ", durationMs=" + std::to_string(trace::nowMs() - stopEnterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::resetWashCar(const WashCarOperation& operation)
{
	// [TEMP_DIAG] resetWashCar 入口时间戳, 算整个函数耗时
	const long long resetEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", mode=" + operation.getMode()
		+ ", point=" + operation.getPoint()
		+ ", enterMs=" + std::to_string(resetEnterMs));

	if (operation.getReqSource() == "mqtt" && !canExecuteCloudCommand()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_MAINTAINING);
		m_currentStatus.setMessage("maintaining");
		LOG_WARNING("[YiGongLiWashCarService] resetWashCar rejected: maintaining mode, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	std::string plcId = operation.getPlcId();
	std::string mode = operation.getMode();
	std::string point = operation.getPoint();

	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();

	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[YiGongLiWashCarService] resetWashCar rejected: no PLC, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	// 驿公里 word 协议无 j 点动模式,保留海天 j 点动检查形式但 word 未启用
	if (mode == "jog") {
		if (!canExecuteJogCommand()) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_JOG_EXECUTING);
			m_currentStatus.setMessage("jog executing");
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar jog rejected: another jog executing, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}

		if (point.empty()) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_JOG_POINT_REQUIRED);
			m_currentStatus.setMessage("point required for jog mode");
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar jog rejected: point missing, boxNo=" + operation.getBoxNo());
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
			LOG_WARNING(std::string("[YiGongLiWashCarService] resetWashCar jog rejected: invalid point format, point=") + point);
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
			LOG_INFO(std::string("[YiGongLiWashCarService] resetWashCar jog done, point=") + pointUpper
				+ ", on=" + (success ? "ok" : "FAIL")
				+ ", off=" + (successReset ? "ok" : "FAIL"));
			return m_currentStatus;
		}
		catch (const std::exception&) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_POINT_ADDRESS);
			m_currentStatus.setMessage("invalid point address");
			LOG_WARNING(std::string("[YiGongLiWashCarService] resetWashCar jog rejected: invalid point address, point=") + point);
			return m_currentStatus;
		}
	}
	else if (mode != "auto" && !mode.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_INVALID_MODE);
		m_currentStatus.setMessage("invalid mode");
		LOG_WARNING(std::string("[YiGongLiWashCarService] resetWashCar rejected: invalid mode, mode=") + mode);
		return m_currentStatus;
	}

	// 调试打印状态表(对称海天,只是改了名称)
	auto fx = fxList.front();
	std::cout << "\n==================== YiGongLi PLC 状态表 ====================\n";
	std::cout << "PLC ID: " << fx.getPlcId() << "\n\n";
	for (const auto& [addr, val] : fx.getYMap()) {
		std::cout << "Y" << addr << " = " << (val ? "ON" : "OFF") << "\n";
	}
	for (const auto& [addr, val] : fx.getMMap()) {
		std::cout << "M" << addr << " = " << (val ? "ON" : "OFF") << "\n";
	}
	for (const auto& [addr, val] : fx.getDMap()) {
		std::cout << "D" << addr << " = " << val << "\n";
	}
	for (const auto& [addr, val] : fx.getXMap()) {
		std::cout << "X" << addr << " = " << (val ? "ON" : "OFF") << "\n";
	}
	for (const auto& [addr, val] : fx.getSMap()) {
		std::cout << "S" << addr << " = " << (val ? "ON" : "OFF") << "\n";
	}
	std::cout << "========================================================\n\n";

	// 驿公里复位(M202/M203),对称海天 M0/M100 复位
	// 前置检查(读缓存): 仅在停止状态(S0)可复位;硬件故障/停止营业中拒绝
	{
		int activeS = scanActiveS(fx);
		bool m148 = false, m501 = false;
		fx.getMBit(yigl::M_HARDWARE_FAULT, m148);
		fx.getMBit(yigl::M_STOP_WORK, m501);
		updateLastKnownState(fx);

		// 仅停止状态(S0)允许复位;洗车中/归位中/待机中拒绝
		// (activeS=-1 表示缓存 S 尚未就绪,放行交给 PLC 决定)
		if (activeS != -1 && activeS != 0) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_REJECTED);
			m_currentStatus.setMessage("reset only valid in stop state (S0), current S" + std::to_string(activeS));
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar rejected: activeS=" + std::to_string(activeS)
				+ ", boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
		// 硬件故障(M148=1): 先处理故障(D1000 报出故障码)
		if (m148) {
			uint16_t d1000 = 0;
			fx.getDRegister(yigl::D_FAULT_CODE, d1000);
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_REJECTED);
			m_currentStatus.setMessage("hardware fault (M148=1, d1000=" + std::to_string(d1000) + ")");
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar rejected: M148=1 d1000=" + std::to_string(d1000)
				+ ", boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
		// 停止营业中(M501=1): 禁止归位
		if (m501) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_REJECTED);
			m_currentStatus.setMessage("stop work active (M501=1)");
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar rejected: M501=1, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	}

	const WashCarAdmission resetAdmission = admitCommand(WashCarCommand::RESET, "resetWashCar");
	if (resetAdmission.blocked) {
		return m_currentStatus;
	}

	const long long resetPlc1EnterMs = trace::nowMs();
	const std::string resetPoint = getServicePoint("reset");
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_PLC_ENTER cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + resetPoint + ", val=true, enterMs=" + std::to_string(resetPlc1EnterMs));
	bool success = m_stateMachine->writePoint(deviceService, plcId, resetPoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_PLC_EXIT cmdId=") + trace::cmdId
		+ ", step=1/2, point=" + resetPoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - resetPlc1EnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	if (success) {
		// 写成功 → 轮询确认点 S1 ON(归位开始),随后继续到 S5(待机=归位完成)
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool s1 = false;
			return fx.getSBit(1, s1) && s1;
		});
		if (confirmed) {
			// 归位完成后 PLC 应回到 S5 待机
			bool homeDone = waitForConfirm([](const FxPlcStatus& fx) {
				bool s5 = false;
				return fx.getSBit(5, s5) && s5;
			});
			m_currentStatus.setCode(homeDone ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
			m_currentStatus.setMessage(homeDone ? "reset success" : "home not completed within timeout");
			LOG_INFO(std::string("[YiGongLiWashCarService] resetWashCar ok, forceFxM(M202,true) success")
				+ (homeDone ? ", home done (S5)" : ", S1 seen but S5 timeout") + ", boxNo=" + operation.getBoxNo());
		} else {
			// 卡在 S1 且 D1000≠0 → 归位被故障卡死
			uint16_t d1000 = 0;
			FxPlcStatus fxNow;
			if (getYiglFxStatus(fxNow)) fxNow.getDRegister(yigl::D_FAULT_CODE, d1000);
			m_currentStatus.setCode(d1000 != 0 ? ErrorCode::Code::WASHCAR_RESET_REJECTED : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
			m_currentStatus.setMessage(d1000 != 0
				? "home stuck by fault (d1000=" + std::to_string(d1000) + ")"
				: "reset not confirmed within timeout");
			LOG_WARNING("[YiGongLiWashCarService] resetWashCar not confirmed, d1000=" + std::to_string(d1000)
				+ ", boxNo=" + operation.getBoxNo());
		}
	} else {
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_FAILED);
		m_currentStatus.setMessage("reset failed");
		LOG_ERROR("[YiGongLiWashCarService] resetWashCar failed: forceFxM(M202,true) rejected, boxNo=" + operation.getBoxNo());
	}

	// 复位没真正完成(写失败/归位超时/被故障卡住)都退回"故障态", 不允许直接启动
	if (m_currentStatus.getCode() != ErrorCode::Code::SUCCESS) {
		rollbackCommand(resetAdmission, "resetWashCar");
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - resetEnterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::getWashCarStatus(const WashCarOperation& operation) {
	const long long readEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_READ_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", queryPoints=" + std::to_string(operation.getQueryPoints().size())
		+ ", enterMs=" + std::to_string(readEnterMs));

	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();

	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_READ_EXIT cmdId=") + trace::cmdId
			+ ", result=no_plc, durationMs=" + std::to_string(trace::nowMs() - readEnterMs));
		return m_currentStatus;
	}

	auto fx = fxList.front();
	m_currentStatus.clearStatusPoints();

	// ===== 驿公里状态解析 =====
	// D1210 是 ASCII 字符串("stop"/"work"/"alarm"/"normal"),不再当数字 step 用
	m_deviceStatusStr = parseD1210(fx);
	int dStatusEnum = 0;
	if (m_deviceStatusStr == "work")      dStatusEnum = 1;
	else if (m_deviceStatusStr == "alarm") dStatusEnum = 2;
	else if (m_deviceStatusStr == "normal") dStatusEnum = 3;
	// else "stop" / 空 → 0

	// S 扫描: 唯一 ON 的 STL 步 = 精确进度/故障定位权威源
	int activeS = scanActiveS(fx);
	m_activeS = activeS;
	uint16_t d1000_raw = 0;
	fx.getDRegister(yigl::D_FAULT_CODE, d1000_raw);
	m_lastKnownD1000 = d1000_raw;

	// currentStep: 工序号(1~10)或 0;故障码(101~121)不进 currentStep
	int step = m_stateMachine->stepNumber(activeS);
	if (d1000_raw >= 1 && d1000_raw <= 10) step = d1000_raw;
	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
	m_currentStatus.setCurrentStep(step < 0 ? 0 : step);
	m_currentStatus.setMessage("read ok");
	// 兼容字段: d1210 保留数字(状态枚举),d1000 原始码,s_active 步号
	m_currentStatus.addStatusPoint("d1210", dStatusEnum);
	m_currentStatus.addStatusPoint("d1000", d1000_raw);
	if (activeS >= 0) {
		m_currentStatus.addStatusPoint("s" + std::to_string(activeS), 1);
	}
	// 读 d2000 洗车时长(对称海天读 s0)
	uint16_t d2000 = 0;
	if (fx.getDRegister(2000, d2000)) {
		m_currentStatus.addStatusPoint("d2000", d2000);
	}

	const auto& queryPoints = operation.getQueryPoints();
	for (const auto& [key, _] : queryPoints) {
		if (key.size() < 2) {
			LOG_WARNING(std::string("[YiGongLiWashCarService] skip invalid query point=") + key);
			continue;
		}
		try {
			if (key.substr(0, 1) == "d" || key.substr(0, 1) == "D") {
				int dNum = std::stoi(key.substr(1));
				uint16_t value = 0;
				if (fx.getDRegister(dNum, value)) {
					m_currentStatus.addStatusPoint(key, value);
				}
			} else if (key.substr(0, 1) == "y" || key.substr(0, 1) == "Y") {
				int yNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getYBit(yNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			} else if (key.substr(0, 1) == "m" || key.substr(0, 1) == "M") {
				int mNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getMBit(mNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			} else if (key.substr(0, 1) == "x" || key.substr(0, 1) == "X") {
				int xNum = std::stoi(key.substr(1));
				bool value = false;
				if (fx.getXBit(xNum, value)) {
					m_currentStatus.addStatusPoint(key, value ? 1 : 0);
				}
			}
		} catch (const std::exception& e) {
			LOG_WARNING(std::string("[YiGongLiWashCarService] skip invalid query point=") + key + ", err=" + e.what());
		}
	}

	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_READ_EXIT cmdId=") + trace::cmdId
		+ ", result=ok, pointCount=" + std::to_string(m_currentStatus.getStatus().size())
		+ ", durationMs=" + std::to_string(trace::nowMs() - readEnterMs));

	// [2026-08-31] 末尾补全: getWashCarStatus 成功 path 必须 setBoxNo + setCode + setMessage
	// 之前如果全部点位都读失败,return 空白 status,调用方会拿到 code=0 message="" 误判成功
	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
	m_currentStatus.setMessage("read ok");
	return m_currentStatus;
}

bool YiGongLiWashCarService::isWashing() const {
    // S20~S81 任一 ON 即洗车中,替代海天式 0~10 step 模型
    if (m_activeS >= yigl::S_WASH_FIRST && m_activeS <= yigl::S_WASH_LAST) return true;
    // 缓存尚未就绪(首次启动 S 未采到)时按工序号兜底
    if (m_activeS < 0) {
        int step = m_currentStatus.getCurrentStep();
        return step > 0 && step < 10;
    }
    return false;
}





WashCarOperationResult YiGongLiWashCarService::clearAlarm(const WashCarOperation& operation)
{
	// 驿公里 clearAlarm 协议未定义(下方只 return success, 不写 PLC), 因此不参与状态迁移:
	// 否则会把"故障态"凭空清成空闲, 现场没有真正复位。
	std::string plcId = operation.getPlcId();

	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();
	if (fxList.empty()) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_NO_PLC);
		m_currentStatus.setMessage("no plc");
		LOG_WARNING("[YiGongLiWashCarService] clearAlarm rejected: no PLC, boxNo=" + operation.getBoxNo());
		return m_currentStatus;
	}

	auto fx = fxList.front();
	bool m202 = false;

	// 驿公里 word 协议无对应 X44(急停检查),占位预留
	if (false) {
		if (!m202) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_ESTOP_NOT_PRESSED);
			m_currentStatus.setMessage("estop not pressed");
			LOG_WARNING("[YiGongLiWashCarService] clearAlarm rejected: estop not pressed, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	}

	// 驿公里 clearAlarm 协议 word 暂未提供,标"待补",先 return success
	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
	m_currentStatus.setMessage("alarm cleared: pending protocol detail");
	LOG_INFO("[YiGongLiWashCarService] clearAlarm: 驿公里协议 word 暂未提供 clearAlarm 命令定义,标 待补,return success,boxNo=" + operation.getBoxNo());
	return m_currentStatus;
}

// =====================================================================
// 驿公里独有命令(抽象类虚函数实装: m206/m501/m510/m203)
// 对应 docs/驿公里_MQTT_协议对接总结.md 1.1 节的"中止洗车" / "停止营业 2" / "停止营业" / "复位(安全归位)"
// 海天那侧(HaiTianWashCarService)同一虚函数直接返 unsupported
// =====================================================================

WashCarOperationResult YiGongLiWashCarService::abortWashCar(const WashCarOperation& operation) {
	std::string plcId = operation.getPlcId();
	const long long enterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_ABORT_ENTER cmdId=") + trace::cmdId
		+ ", plcId=" + plcId
		+ ", enterMs=" + std::to_string(enterMs));

	const long long plcEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_ABORT_PLC_ENTER cmdId=") + trace::cmdId
		+ ", point=M206, val=true, enterMs=" + std::to_string(plcEnterMs));

	// 前置判定: 基础态(S0~S5)下中止无效
	FxPlcStatus fx;
	if (getYiglFxStatus(fx)) {
		int activeS = scanActiveS(fx);
		updateLastKnownState(fx);
		if (activeS >= 0 && activeS <= 5) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_ABORT_INVALID_NOT_WASHING);
			m_currentStatus.setMessage("not washing (S" + std::to_string(activeS) + "), abort ignored");
			LOG_WARNING("[YiGongLiWashCarService] abortWashCar invalid: base state S" + std::to_string(activeS)
				+ ", boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	}

	const std::string abortPoint = getServicePoint("abort");
	bool success = m_stateMachine->writePoint(deviceService, plcId, abortPoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_ABORT_PLC_EXIT cmdId=") + trace::cmdId
		+ ", point=" + abortPoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - plcEnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	if (success) {
		// 写成功 → 轮询确认点: S4 ON 且 D1000=106
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool s4 = false;
			uint16_t d1000 = 0;
			return fx.getSBit(4, s4) && s4
				&& fx.getDRegister(yigl::D_FAULT_CODE, d1000) && d1000 == 106;
		});
		m_currentStatus.setCode(confirmed ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
		m_currentStatus.setMessage(confirmed ? "aborted" : "abort not confirmed (S4/d1000=106) within timeout");
		if (!confirmed) {
			LOG_WARNING("[YiGongLiWashCarService] abortWashCar timeout: M206 written but S4 not reached, boxNo=" + operation.getBoxNo());
		}
	} else {
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_ABORT_FAILED);
		m_currentStatus.setMessage("abort failed");
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_ABORT_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - enterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::stopWork(const WashCarOperation& operation) {
	std::string plcId = operation.getPlcId();
	const long long enterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_WORK_ENTER cmdId=") + trace::cmdId
		+ ", plcId=" + plcId
		+ ", enterMs=" + std::to_string(enterMs));

	const long long plcEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_WORK_PLC_ENTER cmdId=") + trace::cmdId
		+ ", point=M501, val=true, enterMs=" + std::to_string(plcEnterMs));
	const std::string stopWorkPoint = getServicePoint("stopWork");
	bool success = m_stateMachine->writePoint(deviceService, plcId, stopWorkPoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_WORK_PLC_EXIT cmdId=") + trace::cmdId
		+ ", point=" + stopWorkPoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - plcEnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	if (success) {
		// 写成功 → 轮询确认点: S0 ON 或 D1210="stop"
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool s0 = false;
			return (fx.getSBit(0, s0) && s0);
		});
		m_currentStatus.setCode(confirmed ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
		m_currentStatus.setMessage(confirmed ? "stop work success" : "stop work not confirmed within timeout");
		if (!confirmed) {
			LOG_WARNING("[YiGongLiWashCarService] stopWork timeout: M501 written but S0 not ON, boxNo=" + operation.getBoxNo());
		}
	} else {
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STOP_WORK_FAILED);
		m_currentStatus.setMessage("stop work failed");
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_WORK_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - enterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::stopService(const WashCarOperation& operation) {
	std::string plcId = operation.getPlcId();
	const long long enterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_SERVICE_ENTER cmdId=") + trace::cmdId
		+ ", plcId=" + plcId
		+ ", enterMs=" + std::to_string(enterMs));

	const long long plcEnterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_SERVICE_PLC_ENTER cmdId=") + trace::cmdId
		+ ", point=M510, val=true, enterMs=" + std::to_string(plcEnterMs));
	const std::string stopServicePoint = getServicePoint("stopService");
	bool success = m_stateMachine->writePoint(deviceService, plcId, stopServicePoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_SERVICE_PLC_EXIT cmdId=") + trace::cmdId
		+ ", point=" + stopServicePoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - plcEnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	if (success) {
		// 确认点: 读回 M510==1(PLC 程序以 ¬M510 常闭做启动锁/车辆引导闸门, 读回 1 即命令已生效)
		// T128=K15(100ms 单位 → 1.5s), M270 仅 T128∈(K5,K10)
		// 即 M510 置位后 0.5~1.0s 的 500ms 一次性脉冲(外部音屏 LED), 非持续确认位;
		// 采集 1s 采样会漏脉冲, 加长等待窗口无效 → 不用 M270 作确认点, 仅作旁证日志
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool m510 = false;
			return fx.getMBit(yigl::M_STOP_SERVICE, m510) && m510;
		});
		if (confirmed) {
			m_isServicePaused = true;   // 自身状态置暂停(断连时也拒绝启动)
			bool m270 = false;
			bool hasM270 = false;
			FxPlcStatus fxPause;
			if (getYiglFxStatus(fxPause)) hasM270 = fxPause.getMBit(yigl::M_PAUSE_LED, m270);
			m_currentStatus.setCode(ErrorCode::Code::SUCCESS);
			m_currentStatus.setMessage("stop service success (paused)");
			LOG_INFO(std::string("[YiGongLiWashCarService] stopService confirmed: M510=1, service paused, m270Observed=")
				+ (hasM270 ? (m270 ? "1" : "0") : "unread")
				+ ", boxNo=" + operation.getBoxNo());
		} else {
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
			m_currentStatus.setMessage("pause not confirmed (M510) within timeout");
			LOG_WARNING("[YiGongLiWashCarService] stopService timeout: M510 written but read-back not 1, boxNo=" + operation.getBoxNo());
		}
	} else {
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_STOP_SERVICE_FAILED);
		m_currentStatus.setMessage("stop service failed");
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_STOP_SERVICE_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - enterMs));
	return m_currentStatus;
}

WashCarOperationResult YiGongLiWashCarService::resetSafe(const WashCarOperation& operation) {
	std::string plcId = operation.getPlcId();
	const long long enterMs = trace::nowMs();
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_SAFE_ENTER cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", enterMs=" + std::to_string(enterMs));

	const long long plcEnterMs = trace::nowMs();

	// 前置判定: M147=1(停车位有车/不安全)→ 安全归位被拒
	FxPlcStatus fx;
	if (getYiglFxStatus(fx)) {
		bool m147 = false;
		fx.getMBit(yigl::M_UNSAFE, m147);
		updateLastKnownState(fx);
		if (m147) {
			m_currentStatus.setBoxNo(operation.getBoxNo());
			m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_SAFE_REJECTED);
			m_currentStatus.setMessage("safe reset rejected: parking occupied (M147=1)");
			LOG_WARNING("[YiGongLiWashCarService] resetSafe rejected: M147=1, boxNo=" + operation.getBoxNo());
			return m_currentStatus;
		}
	}

	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_SAFE_PLC_ENTER cmdId=") + trace::cmdId
		+ ", point=M203, val=true, enterMs=" + std::to_string(plcEnterMs));
	const std::string resetSafePoint = getServicePoint("resetSafe");
	bool success = m_stateMachine->writePoint(deviceService, plcId, resetSafePoint, true);
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_SAFE_PLC_EXIT cmdId=") + trace::cmdId
		+ ", point=" + resetSafePoint + ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - plcEnterMs));

	m_currentStatus.setBoxNo(operation.getBoxNo());
	if (success) {
		// 写成功 → 轮询确认点: S1 ON(归位开始)
		bool confirmed = waitForConfirm([](const FxPlcStatus& fx) {
			bool s1 = false;
			return fx.getSBit(1, s1) && s1;
		});
		m_currentStatus.setCode(confirmed ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT);
		m_currentStatus.setMessage(confirmed ? "reset safe success" : "reset safe not confirmed within timeout");
		if (!confirmed) {
			LOG_WARNING("[YiGongLiWashCarService] resetSafe timeout: M203 written but S1 not ON, boxNo=" + operation.getBoxNo());
		}
	} else {
		m_currentStatus.setCode(ErrorCode::Code::WASHCAR_RESET_SAFE_FAILED);
		m_currentStatus.setMessage("reset safe failed");
	}
	LOG_INFO(std::string("[YiGongLiWashCarService] WASHCAR_RESET_SAFE_EXIT cmdId=") + trace::cmdId
		+ ", boxNo=" + operation.getBoxNo()
		+ ", result=" + (success ? "ok" : "FAIL")
		+ ", durationMs=" + std::to_string(trace::nowMs() - enterMs));
	return m_currentStatus;
}

// =====================================================================
// 点位操作(对称海天,改 forceFxM)
// =====================================================================



WashCarOperationResult YiGongLiWashCarService::pointReset(const WashCarOperation& operation) {
    std::string plcId = operation.getPlcId();

    if (operation.getPoint() == "all") {
        m_stateMachine->writePoint(deviceService, plcId, getServicePoint("reset"), true);
        bool success = m_stateMachine->writePoint(deviceService, plcId, getServicePoint("resetSafe"), true);

        m_currentStatus.setBoxNo("all");
        m_currentStatus.setCode(success ? ErrorCode::Code::SUCCESS : ErrorCode::Code::WASHCAR_RESET_FAILED);
        m_currentStatus.setMessage(success ? "reset success" : "reset failed");
        LOG_INFO(std::string("[YiGongLiWashCarService] pointReset all, plcId=") + plcId
            + ", result=" + (success ? "ok" : "FAIL"));
    } else {
        m_currentStatus.setBoxNo(operation.getBoxNo());
        m_currentStatus.setCode(ErrorCode::Code::WASHCAR_POINT_RESET_UNSUPPORTED);
        m_currentStatus.setMessage("point reset: implementation pending confirmation");
        LOG_WARNING(std::string("[YiGongLiWashCarService] pointReset rejected: only 'all' supported, got point=") + operation.getPoint());
    }

    return m_currentStatus;
}


void YiGongLiWashCarService::startWashCarStatusMonitor(const WashCarOperation& operation, MqttSender sender) {
    if (m_statusMonitorRunning) return;
    m_statusMonitorRunning = true;
    m_monitorOperation = operation;
    m_mqttSender = sender;
    LOG_INFO("[YiGongLiWashCarService] startWashCarStatusMonitor, boxNo=" + operation.getBoxNo()
             + ", reqSource=" + operation.getReqSource());
    m_timer.addTask("washcar_monitor",
                    ConfigManager::instance().timing().washcarMonitorIntervalMs,
                    TimerTaskMode::WORKER,
                    [this]() { onMonitorTick(); });
}

void YiGongLiWashCarService::onMonitorTick() {
    if (!m_statusMonitorRunning) return;

    const auto status = getWashCarStatus(m_monitorOperation);

    if (m_monitorOperation.getReqSource() == "mqtt") {
        json msg;
        msg["d1210"] = status.getCurrentStep();
        msg["d2000"] = 0;
        for (const auto& [k, v] : status.getStatus()) {
            if (k == "d2000") { msg["d2000"] = v; break; }
        }
        msg["deviceStatus"] = m_deviceStatusStr.empty() ? "unknown" : m_deviceStatusStr;
        if (m_activeS >= 0) {
            msg["sState"] = m_activeS;
            msg["stepName"] = m_stateMachine->stepName(m_activeS);
        }
        addCommonFlexemFields(msg, m_monitorOperation.getBoxNo());
        LOG_DEBUG("[YiGongLiWashCarService] d1210=" + std::to_string(status.getCurrentStep())
                  + ", deviceStatus=" + m_deviceStatusStr
                  + ", sState=" + std::to_string(m_activeS));
        m_mqttSender(Command::Endpoint::MONITOR, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_STATUS);
    }

    // 完成 = S81; 故障 = S3/S4 或 D1000∈[101,121]
    const bool washing = (m_activeS >= yigl::S_WASH_FIRST && m_activeS <= yigl::S_WASH_LAST);
    const bool completed = (m_activeS == 81);
    const bool faulted = (m_activeS == 3 || m_activeS == 4)
                      || (m_lastKnownD1000 >= 101 && m_lastKnownD1000 <= 121);

    const char* stopReason = nullptr;
    if (!washing) stopReason = "not_washing";
    else if (completed) stopReason = "completed";
    else if (faulted) stopReason = "fault";

    if (stopReason != nullptr) {
        LOG_INFO(std::string("[YiGongLiWashCarService] WashCarStatusMonitor stopped: ") + stopReason
                 + ", sState=" + std::to_string(m_activeS));
        m_statusMonitorRunning = false;
        m_timer.removeTask("washcar_monitor");
    }
}

void YiGongLiWashCarService::executeDailyReportTask(MqttSender sender) {
    time_t now = time(nullptr);
    tm* tm_now = localtime(&now);
    std::cout << "[YiGongLiWashCarService] executeDailyReportTask: 当前时间："
        << tm_now->tm_hour << "时"
        << tm_now->tm_min << "分"
        << tm_now->tm_sec << "秒" << std::endl;
    LOG_DEBUG("[YiGongLiWashCarService] executeDailyReportTask tick");

    // 驿公里 word 协议无 dailyReportPoints 字段(无物料/水位监控点)
    // docs/驿公里_MQTT_协议对接总结.md 明确"待补",此处保留形式但不触发上报
    if (tm_now->tm_hour == ConfigManager::instance().timing().dailyReportHour &&
        tm_now->tm_min  == ConfigManager::instance().timing().dailyReportMinute) {
        std::vector<std::pair<std::string, int>> points;
        collectDailyReportPoints(points);

        if (points.empty()) {
            // 驿公里无 daily 上报点位,跳过
            return;
        }

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
            if (val != 0) {
                hasExceptionReport = true;
            }
        }

        if (hasExceptionReport) {
            addCommonFlexemFields(msg, operation.getBoxNo());

            std::cout << "[YiGongLiWashCarService] Timer send DailyReport (Exception)" << std::endl;
            LOG_WARNING("[YiGongLiWashCarService] DailyReport exception published, keys=" + std::to_string(msg.size()));
            sender(Command::Endpoint::PUSHALARM, msg.dump(), MessageBuffer::MessageType::TYPE_TIMER_REPORT);
        }
    }
}


void YiGongLiWashCarService::executeFaultReportTask(MqttSender sender) {
    auto allStatus = deviceService.viewAllDeviceStatus();
    const auto& fxList = allStatus.getFxPlcStatusList();
    if (fxList.empty()) return;

    const auto& fx = fxList.front();

    // 故障报文由状态机组(驿公里稀疏:故障位偏离正常值才带,故障码取 101~121)
    const auto points = toPlcPointValues(fx);
    json msg = m_stateMachine->buildFaultReport(points);
    int faultCodeValue = 0;
    m_stateMachine->readPoint(points, "D1000", faultCodeValue);   // 日志要带实际值,便于调试

    // 状态没变不发;变了才发并记下,等下次变化
    if (msg == m_lastFaultReport) return;

    // 故障全清:用上一帧出现过的字段发一条 0 值报文,让云端知道已恢复。
    // 驿公里文档只定义了"触发时带点位",没有定义清除报文;这里发一次零值,
    // 之后 msg 与 last 都为空,不会再重复。
    if (msg.empty()) {
        json clearMsg = json::object();
        for (const auto& [key, value] : m_lastFaultReport.items()) {
            if (value.is_number()) clearMsg[key] = 0;
        }
        m_lastFaultReport = json::object();
        if (clearMsg.empty()) return;
        addCommonFlexemFields(clearMsg, "");
        LOG_WARNING("[YiGongLiWashCarService] FaultReport cleared, payload=" + clearMsg.dump());
        sender(Command::Endpoint::PUSHALARM, clearMsg.dump(), MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        return;
    }

    m_lastFaultReport = msg;
    addCommonFlexemFields(msg, "");

    // 日志保留 d1000_actual / d1000_is_fault 字段,便于调试
    LOG_WARNING("[YiGongLiWashCarService] FaultReport, count=" + std::to_string(msg.size())
        + ", d1000_actual=" + std::to_string(faultCodeValue)
        + ", d1000_is_fault=" + std::string(msg.contains("d1000") ? "true" : "false")
        + ", payload=" + msg.dump());
    sender(Command::Endpoint::PUSHALARM, msg.dump(), MessageBuffer::MessageType::TYPE_FAULT_REPORT);
}



