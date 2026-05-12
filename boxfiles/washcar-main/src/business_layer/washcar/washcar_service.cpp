#include "business_layer/washcar/washcar_service.h"
#include <iostream>

WashCarService::WashCarService(IDeviceService& deviceService) : deviceService(deviceService) {

}


WashCarService::~WashCarService()
{

}

WashCarOperationResult WashCarService::startWashCar(const WashCarOperation& operation)
{
	std::string plcId = operation.getPlcId();

	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();
    if (fxList.empty()) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("no plc");
		return m_currentStatus;
	}
	auto fx = fxList.front();

	bool y52 = false, y74 = false;
	bool m381 = false, m187 = false, m1 = false;
    //确保当前y52/y74/m381状态为0，m187/m1状态为1
	if (fx.getYBit(52, y52) || fx.getYBit(74, y74) || fx.getMBit(381, m381)) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("safety check failed");
		return m_currentStatus;
	}else if (!fx.getMBit(187, m187) == 0 || !fx.getMBit(1, m1) == 0) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("safety check failed");
		return m_currentStatus;
	}

	if (!checkCurrentAlarms()) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("alarm exists");
		return m_currentStatus;
	}

    // 下发都设置为true
    bool success = deviceService.forceFxM(plcId, 242, true);
	if (success) {
		m_currentStatus.setBoxNo(operation.getBoxNo());
		m_currentStatus.setCode(0);
		m_currentStatus.setMessage("success");
		m_currentStatus.setCurrentStep(0);
	} else {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("plc command failed");
	}
	return m_currentStatus;
}

WashCarOperationResult WashCarService::stopWashCar(const WashCarOperation& operation)
{
	std::string plcId = operation.getPlcId();
    // 下发都设置为true
    bool success = deviceService.forceFxM(plcId, 0, true);

	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(success ? -2 : -1);
	m_currentStatus.setMessage(success ? "stopped" : "stop failed");
	return m_currentStatus;
}

WashCarOperationResult WashCarService::resetWashCar(const WashCarOperation& operation)
{
	std::string plcId = operation.getPlcId();

    // 在发复位指令（m100=1)前先读m0，如果m0=1,要先发m0=0
	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();
	if (fxList.empty()) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("no plc");
		return m_currentStatus;
	}

	auto fx = fxList.front();
	bool m0 = false;
    fx.getMBit(0, m0);
    if (m0 == 1) {
        deviceService.forceFxM(plcId, m0, false);
    }

    // 下发都设置为true
	bool success = deviceService.forceFxM(plcId, 100, true);

	m_currentStatus.setBoxNo(operation.getBoxNo());
	m_currentStatus.setCode(success ? 0 : -1);
	m_currentStatus.setMessage(success ? "reset success" : "reset failed");
	m_currentStatus.setCurrentStep(0);
	return m_currentStatus;
}

WashCarOperationResult WashCarService::getWashCarStatus(const WashCarOperation& operation) {
	BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
	const auto& fxList = directStatus.getFxPlcStatusList();
	
	if (fxList.empty()) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("no plc");
		return m_currentStatus;
	}
	
	auto fx = fxList.front();
	
	uint16_t d142 = 0;
	if (fx.getDRegister(142, d142)) {
        m_currentStatus.setCode(0);
		m_currentStatus.setCurrentStep(d142);
	}
	
	bool hasFault = !checkCurrentAlarms();
	if (hasFault) {
		m_currentStatus.setCode(-1);
		m_currentStatus.setMessage("fault");
	}
	
	return m_currentStatus;
}

bool WashCarService::isWashing() const {
	return m_currentStatus.getCode() == 0 && m_currentStatus.getCurrentStep() > 0 && m_currentStatus.getCurrentStep() < 10;
}

bool WashCarService::checkCurrentAlarms() {
    // 获取当前设备状态
    BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();
    const auto& fxList = directStatus.getFxPlcStatusList();

    // 无PLC → 禁止启动
    if (fxList.empty()) {
        return false;
    }

    // 取第一个FX PLC（和你启动逻辑一致）
    const auto& fx = fxList.front();
    bool state = false;

    // 1. 急停 X44  按下=1 → 禁止启动
    if (fx.getXBit(44, state)) {
        if (state) {
            return false;
        }
    }
    else {
        // 读取失败 → 安全起见禁止启动
        return false;
    }

    // 2. 水位不足 X50 =1 → 禁止启动
    if (fx.getXBit(50, state)) {
        if (state) {
            return false;
        }
    }
    else {
        return false;
    }

    // 3. 所有 faultRegister 故障点位（必须全为0）
    int faultMList[] = {
        3,      // faultRegister-1  车头斜度报警
        23,     // faultRegister-2  升降变频器报警
        26,     // faultRegister-3  车辆撞击
        31,     // faultRegister-4  行走变频器报警
        32,     // faultRegister-5  横刷旋转热继电器
        32,     // faultRegister-6  横刷旋转热继电器
        33,     // faultRegister-7  立刷旋转热继电器
        34,     // faultRegister-8  轮刷旋转热继电器
        35,     // faultRegister-9  左立刷行走热继电器
        36,     // faultRegister-10 右立刷行走热继电器
        37,     // faultRegister-11 水泵热继电器
        40,     // faultRegister-12 风机1热继电器
        41,     // faultRegister-13 风机2热继电器
        42,     // faultRegister-14 风机3热继电器
        43,     // faultRegister-15 风机4热继电器
        220,    // faultRegister-16 行走前进报警
        221,    // faultRegister-17 行走后退报警
        222,    // faultRegister-18 码盘报警
        223,    // faultRegister-19 横刷电流过大报警
        224,    // faultRegister-20 立刷感应器报警
        225,    // faultRegister-21 横刷电流过小报警
        400,    // faultRegister-22 车辆撞击
        47,     // faultRegister-23 纯净水感应器报警
    };

    for (int mAddr : faultMList) {
        if (fx.getMBit(mAddr, state)) {
            if (state) {
                // 任意故障=1 → 禁止启动
                return false;
            }
        }
        else {
            return false;
        }
    }

    //// 4. 无故障标志 M250 必须 = 1
    //if (fx.getMBit(250, state)) {
    //    if (!state) {
    //        return false;
    //    }
    //}
    //else {
    //    return false;
    //}

    //// 5. 报警寄存器 alarmRegister（必须全为0）
    //int alarmMList[] = {
    //    44,     // alarmRegister-1 洗车液报警
    //    45,     // alarmRegister-2 蜡报警
    //};

    //for (int mAddr : alarmMList) {
    //    if (fx.getMBit(mAddr, state)) {
    //        if (state) {
    //            return false;
    //        }
    //    }
    //    else {
    //        return false;
    //    }
    //}

    // 所有检查全部正常 → 允许启动
    return true;
}