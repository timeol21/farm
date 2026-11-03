#include "business_layer/washcar/washcar_state_machine.h"
#include "business_layer/device/device_service.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace washcar::cfg;

namespace {

const SignalConfig* findSignal(const MachinePointTable& pointTable, const std::string& name) {
    for (const auto& signal : pointTable.signals) {
        if (signal.name == name) return &signal;
    }
    return nullptr;
}

const ServiceConfig* findService(const MachinePointTable& pointTable, const std::string& name) {
    for (const auto& service : pointTable.services) {
        if (service.name == name) return &service;
    }
    return nullptr;
}

}  // namespace

WashCarStateMachine::WashCarStateMachine(const std::string& boxType)
    : pointTable_(ConfigManager::instance().machine(boxType)) {}

PlcPointValues toPlcPointValues(const FxPlcStatus& status) {
    PlcPointValues points;
    for (const auto& [addr, value] : status.getYMap()) points.values["Y" + std::to_string(addr)] = value ? 1 : 0;
    for (const auto& [addr, value] : status.getMMap()) points.values["M" + std::to_string(addr)] = value ? 1 : 0;
    for (const auto& [addr, value] : status.getXMap()) points.values["X" + std::to_string(addr)] = value ? 1 : 0;
    for (const auto& [addr, value] : status.getSMap()) points.values["S" + std::to_string(addr)] = value ? 1 : 0;
    for (const auto& [addr, value] : status.getDMap()) points.values["D" + std::to_string(addr)] = value;
    return points;
}

const char* toString(ServiceState state) {
    switch (state) {
    case ServiceState::IDLE:    return "空闲";
    case ServiceState::WORKING: return "运作";
    case ServiceState::FAULT:   return "故障";
    }
    return "未知";
}

bool WashCarStateMachine::readPoint(const PlcPointValues& points, const std::string& point, int& out) const {
    out = 0;
    if (point.size() < 2 || !points.has(point)) return false;
    out = points.at(point);
    return true;
}

bool WashCarStateMachine::writePoint(IDeviceService& deviceService, const std::string& plcId,
                                     const std::string& point, bool value) const {
    if (point.size() < 2) return false;
    const int addr = std::atoi(point.c_str() + 1);
    if (point[0] == 'M') return deviceService.forceFxM(plcId, addr, value);
    if (point[0] == 'X') return deviceService.forceFxX(plcId, addr, value);
    return false;
}

std::string WashCarStateMachine::servicePoint(const std::string& serviceName,
                                              const std::string& selector) const {
    const ServiceConfig* service = findService(pointTable_, serviceName);
    if (!service) return {};
    auto entry = service->write.find(selector);
    if (entry == service->write.end() && selector != "default") {
        entry = service->write.find("default");
    }
    return entry == service->write.end() ? std::string{} : entry->second;
}

std::string WashCarStateMachine::signalPoint(const std::string& signalName) const {
    const SignalConfig* signal = findSignal(pointTable_, signalName);
    return signal ? signal->point : std::string{};
}

std::string WashCarStateMachine::stepName(int) const {
    return {};
}

int WashCarStateMachine::stepNumber(int) const {
    return 0;
}

std::string WashCarStateMachine::cloudKey(const std::string& signalName) const {
    const SignalConfig* signal = findSignal(pointTable_, signalName);
    if (!signal) return {};
    if (!signal->cloudKey.empty()) return signal->cloudKey;
    std::string key = signal->point;
    for (auto& character : key) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return key;
}

int WashCarStateMachine::getSignalValue(const PlcPointValues& points, const std::string& name) const {
    const SignalConfig* signal = findSignal(pointTable_, name);
    return signal ? points.at(signal->point) : 0;
}

bool WashCarStateMachine::isSignalNormal(const PlcPointValues& points, const std::string& name) const {
    const SignalConfig* signal = findSignal(pointTable_, name);
    if (!signal) return true;
    if (!points.has(signal->point)) return true;   // 没采到就不判定
    return points.at(signal->point) == signal->normalValue;
}

MachineStatus WashCarStateMachine::buildStatus(const PlcPointValues& points) const {
    MachineStatus status;
    status.washing = isWashing(points);

    for (const auto& name : pointTable_.faultSignals) {
        if (!isSignalNormal(points, name)) {
            status.hasFault = true;
            break;
        }
    }
    for (const auto& name : pointTable_.resetSignals) {
        if (getSignalValue(points, name) != 0) {
            status.needsReset = true;
            break;
        }
    }
    status.faultCode = getSignalValue(points, "fault");

    if (status.hasFault)      status.state = ServiceState::FAULT;
    else if (status.washing)  status.state = ServiceState::WORKING;
    else                      status.state = ServiceState::IDLE;

    WashCarCommandResult startCheck;
    checkStart(points, startCheck);
    status.allowStart = startCheck.allowed;
    status.blockReason = startCheck.allowed ? std::string() : startCheck.reason;
    return status;
}

WashCarCommandResult WashCarStateMachine::checkCommand(const PlcPointValues& points,
                                                       const MachineStatus& status,
                                                       bool maintenance,
                                                       WashCarCommand command) const {
    WashCarCommandResult result;
    switch (command) {
    case WashCarCommand::QUERY:
    case WashCarCommand::STOP:      // 停止永远允许:故障时也得能停,洗车中也得能停
    case WashCarCommand::RESET:     // 复位是"把机器恢复干净"的手段,不该被挡
        result.allowed = true;
        return result;

    case WashCarCommand::ENTER_MAINTENANCE:
        if (maintenance) {
            result.reason = "已在维修模式";
            result.suggestion = "无需重复进入";
        } else if (status.state == ServiceState::WORKING) {
            result.reason = "洗车中不能进入维修模式";
            result.suggestion = "洗车中请先停止";
        } else {
            result.allowed = true;
        }
        return result;

    case WashCarCommand::EXIT_MAINTENANCE:
        if (!maintenance) {
            result.reason = "当前不在维修模式";
            result.suggestion = "无需退出维修";
            return result;
        }
        result.allowed = true;
        return result;

    case WashCarCommand::POINT_ON:
    case WashCarCommand::POINT_OFF:
        // 单点控制只在维修模式:洗车中强制点位有安全风险
        if (maintenance) {
            result.allowed = true;
            return result;
        }
        result.reason = "单点控制只允许在维修模式执行";
        result.suggestion = "请先停止洗车并进入维修模式";
        return result;

    case WashCarCommand::START:
        checkStart(points, result);
        return result;
    }
    result.reason = "未知命令";
    result.suggestion = "检查请求的命令类型";
    return result;
}

bool WashCarStateMachine::checkAndApply(const PlcPointValues& points, bool maintenance,
                                         WashCarCommand command, const char* commandName) {
    // 没采到点位 = 采集没起来或 PLC 断连。宁可放行,也不要在断连时把顾客的启动请求全拒了。
    if (points.values.empty()) return false;

    const auto status = buildStatus(points);
    const auto decision = checkCommand(points, status, maintenance, command);
    if (decision.allowed) return false;

    const bool enforce = ConfigManager::instance().stateMachine().mode == "enforce";
    LOG_INFO(std::string("[WashCarStateMachine] SM_REJECT cmd=") + commandName
        + " enforce=" + (enforce ? "1" : "0")
        + " | " + describe(status, maintenance)
        + " | reason=\"" + decision.reason + "\""
        + " | suggestion=\"" + decision.suggestion + "\"");

    return enforce;
}

void WashCarStateMachine::noteResetConfirmed(const PlcPointValues& points, bool maintenance) {
    if (points.values.empty()) return;
    if (pointTable_.homeSensors.empty()) return;

    const auto status = buildStatus(points);
    // 清单不全时一律判不通过 —— 宁可判不通过,也不要误判通过
    bool passed = !status.hasFault;
    for (const auto& name : pointTable_.homeSensors) {
        if (!passed) break;
        const SignalConfig* signal = findSignal(pointTable_, name);
        if (signal && points.at(signal->point) != 1) passed = false;
    }

    if (passed) {
        LOG_INFO("[WashCarStateMachine] 复位后安全自检通过");
        return;
    }
    LOG_WARNING("[WashCarStateMachine] 复位后安全自检未通过(故障未消除或部件未归位),"
                "机器可能仍需再次复位");
}

std::string WashCarStateMachine::describe(const MachineStatus& status, bool maintenance) const {
    std::string text = std::string("状态=") + toString(status.state)
        + " 允许启动=" + (status.allowStart ? "是" : "否")
        + " 需复位=" + (status.needsReset ? "是" : "否")
        + " 维修=" + (maintenance ? "是" : "否");

    if (!status.blockReason.empty()) text += " 挡住启动=" + status.blockReason;

    if (status.hasFault && status.faultCode != 0) {
        const std::string key = std::to_string(status.faultCode);
        const auto entry = pointTable_.faultCodes.find(key);
        text += " 故障码=" + key;
        text += (entry != pointTable_.faultCodes.end()) ? "(" + entry->second + ")"
                                                       : "(未登记的码值)";
    }
    return text;
}

std::string WashCarStateMachine::describeNow(const PlcPointValues& points, bool maintenance) {
    if (points.values.empty()) return "状态机未判定(PLC 数据未就绪)";
    return describe(buildStatus(points), maintenance);
}

bool WashCarStateMachine::loadMaintenanceFlag(const std::string& storageRoot) {
    if (storageRoot.empty()) return false;
    const std::string path = (std::filesystem::path(storageRoot) / "maintenance.flag").string();
    std::ifstream in(path);
    if (!in.is_open()) return false;
    int value = 0;
    in >> value;
    return value == 1;
}

bool WashCarStateMachine::saveMaintenanceFlag(const std::string& storageRoot, bool maintaining) {
    if (storageRoot.empty()) return false;
    const std::string path = (std::filesystem::path(storageRoot) / "maintenance.flag").string();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << (maintaining ? 1 : 0);
    out.close();
    return out.good();
}

nlohmann::json WashCarStateMachine::buildValueChangeReport(const PlcPointValues& points) const {
    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (signal.report != SignalReport::REPORT_PROPERTY) continue;
        if (!points.has(signal.point)) continue;   // 没变化的点不报
        int value = 0;
        readPoint(points, signal.point, value);
        report[cloudKey(signal.name)] = value;
    }
    return report;
}

HaiTianStateMachine::HaiTianStateMachine() : WashCarStateMachine("haiTian") {}

bool HaiTianStateMachine::isWashing(const PlcPointValues& points) const {
    return getSignalValue(points, "washing") != 0;
}

bool HaiTianStateMachine::checkStart(const PlcPointValues& points, WashCarCommandResult& result) const {
    // 4 条规则照搬原服务层 startWashCar 的前置检查,message 字面值保持不变。
    // 读不到点位 = 放行(原实现是"读到且为 0 才拒绝")。
    if (points.has("M187") && points.at("M187") == 0) {
        result.allowed = false;
        result.reason = (points.has("X3") && points.at("X3") == 1) ? "parking advanced"
                                                                  : "washPosition incorrect";
        result.suggestion = "请确认车辆已停放到指定位置";
        return false;
    }
    if (points.has("M1") && points.at("M1") == 0) {
        result.allowed = false;
        result.reason = "startCondition not";
        result.suggestion = "启动前置条件不满足,请确认车辆和设备状态";
        return false;
    }
    const bool barLeftHit  = points.has("X62") && points.at("X62") == 0;
    const bool barRightHit = points.has("X63") && points.at("X63") == 0;
    if (barLeftHit || barRightHit) {
        result.allowed = false;
        result.reason = "vehicle hit anti-collision bar";
        result.suggestion = "车辆压到防撞杆,请把车挪开后重试";
        return false;
    }
    if (points.has("X46") && points.at("X46") == 0) {
        result.allowed = false;
        result.reason = "vehicle too tall";
        result.suggestion = "车辆高度超出洗车机允许范围";
        return false;
    }
    result.allowed = true;
    return true;
}

nlohmann::json HaiTianStateMachine::buildFaultReport(const PlcPointValues& points) const {
    // 海天稠密:事件类点位全带值,故障恢复时对应点位变 0,云端据此清故障
    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (signal.report != SignalReport::REPORT_EVENT) continue;
        int value = 0;
        readPoint(points, signal.point, value);
        report[cloudKey(signal.name)] = value;
    }
    return report;
}

YiGongLiStateMachine::YiGongLiStateMachine() : WashCarStateMachine("yiGongLi") {}

bool YiGongLiStateMachine::isWashing(const PlcPointValues& points) const {
    const int activeStep = scanActiveStep(points);
    return activeStep >= 20 && activeStep <= 81;
}

int YiGongLiStateMachine::scanActiveStep(const PlcPointValues& points) const {
    // STL 步任意时刻唯一 ON;采集清单是 S0~S81 有序的,多个步同时 ON(异常)时取步号最小的
    for (const auto& point : pointTable_.acquisition.sPoints) {
        if (point.empty() || point[0] != 'S') continue;
        if (points.at(point) != 0) return std::atoi(point.c_str() + 1);
    }
    return -1;
}

int YiGongLiStateMachine::stepNumber(int step) const {
    // 工序号用于 d1210 数字字段兼容,精确进度看 stepName
    if (step == 0 || step == 5 || (step >= 1 && step <= 4)) return 0;
    if (step >= 20 && step <= 81) {
        if (step <= 22) return 1;   // S20 洗车准备 / S21 前进泡沫 / S22 顶刷下
        if (step <= 34) return 2;   // S30~S34 找车头a/洗车头a/洗车身a/洗车尾a/洗后角
        if (step <= 40) return 3;   // S40 后退洗车身a
        if (step <= 57) return 4;   // S50~S57 刷子准备/洗车尾b/洗车身b/找车头b/洗车头b/前洗身b/退喷淋/打蜡
        if (step <= 72) return 5;   // S70~S72 风干
        if (step == 80) return 6;   // S80 正常归位
        if (step == 81) return 10;  // S81 洗车完成
    }
    return -1;
}

std::string YiGongLiStateMachine::stepName(int step) const {
    switch (step) {
    case 0:  return "stop";
    case 1:  return "home1";
    case 2:  return "home2";
    case 3:  return "alarm_home";
    case 4:  return "abort";
    case 5:  return "standby";
    case 20: return "prep";
    case 21: return "foam_forward";
    case 22: return "top_brush_down";
    case 30: return "find_front_a";
    case 31: return "wash_front_a";
    case 32: return "wash_body_a_fwd";
    case 33: return "wash_rear_a";
    case 34: return "wash_rear_corner";
    case 40: return "wash_body_a_bwd";
    case 50: return "brush_prep";
    case 51: return "wash_rear_b";
    case 52: return "wash_body_b_bwd";
    case 53: return "find_front_b";
    case 54: return "wash_front_b";
    case 55: return "wash_body_b_fwd";
    case 56: return "rinse_bwd";
    case 57: return "wax_fwd";
    case 70: case 72: return "dry_bwd";
    case 71: return "dry_fwd";
    case 80: return "normal_home";
    case 81: return "completed";
    default: return "S" + std::to_string(step);
    }
}

bool YiGongLiStateMachine::checkStart(const PlcPointValues& points, WashCarCommandResult& result) const {
    // 3 条规则照搬原服务层 startWashCar 的前置检查,message 字面值保持不变。
    // 模式校验和"自身暂停服务"不在这里 —— 前者是命令参数,后者是服务自身的运行状态。
    const int activeStep = scanActiveStep(points);
    if (activeStep != -1 && activeStep != 5) {
        result.allowed = false;
        result.reason = "not in standby (S" + std::to_string(activeStep) + ")";
        result.suggestion = "设备不在待机状态,洗车中/归位中/停止中都不能启动";
        return false;
    }
    if (points.has("M510") && points.at("M510") != 0) {
        result.allowed = false;
        result.reason = "paused service (M510=1)";
        result.suggestion = "服务已暂停,先恢复服务再启动";
        return false;
    }
    // 原实现读不到 M282 时按"没到位"处理,所以这里缺点位也拒
    if (!points.has("M282") || points.at("M282") == 0) {
        result.allowed = false;
        result.reason = "car not positioned (M282=0)";
        result.suggestion = "请把车停到指定位置";
        return false;
    }
    result.allowed = true;
    return true;
}

nlohmann::json YiGongLiStateMachine::buildFaultReport(const PlcPointValues& points) const {
    // 驿公里稀疏:故障位偏离正常值才带;故障码取 D1000,有效区间 101~121
    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (signal.report != SignalReport::REPORT_EVENT) continue;
        int value = 0;
        if (!readPoint(points, signal.point, value)) continue;

        if (signal.point[0] == 'D') {
            if (value < 101 || value > 121) continue;
            report[cloudKey(signal.name)] = value;
            const auto entry = pointTable_.faultCodes.find(std::to_string(value));
            if (entry != pointTable_.faultCodes.end()) report["faultName"] = entry->second;
            const int activeStep = scanActiveStep(points);
            if (activeStep >= 0) {
                report["sState"] = activeStep;
                report["stepName"] = stepName(activeStep);
            }
        } else if (value != signal.normalValue) {
            report[cloudKey(signal.name)] = 1;
        }
    }
    return report;
}

nlohmann::json YiGongLiStateMachine::buildValueChangeReport(const PlcPointValues& points) const {
    nlohmann::json report = WashCarStateMachine::buildValueChangeReport(points);
    const int activeStep = scanActiveStep(points);
    if (activeStep >= 0) {
        report["sState"] = activeStep;
        report["stepName"] = stepName(activeStep);
    }
    return report;
}
