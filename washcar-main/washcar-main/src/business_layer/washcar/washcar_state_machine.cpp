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
    points.plcId = status.getDeviceId();
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
    case ServiceState::RUNNING: return "运行";
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

MachineStatus WashCarStateMachine::buildStatusWith(const PlcPointValues& points,
                                                   ServiceState state) const {
    MachineStatus status;
    status.state = state;
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

    WashCarCheckResult startCheck;
    checkStart(points, startCheck);
    status.allowStart = startCheck.allowed;
    status.blockReason = startCheck.allowed ? std::string() : startCheck.reason;
    return status;
}

ServiceState WashCarStateMachine::state() const {
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_state;
}

void WashCarStateMachine::setStateLocked(ServiceState next, const char* trigger) const {
    if (m_state == next) return;
    const ServiceState previous = m_state;
    m_state = next;
    if (ConfigManager::instance().stateMachine().logStateTransition) {
        LOG_INFO(std::string("[WashCarStateMachine] SM_STATE ") + toString(previous)
                 + " -> " + toString(next) + ", trigger=" + trigger);
    }
}

// 从软元件里构建洗车机状态，正在洗车状态为主动设置不在此处
MachineStatus WashCarStateMachine::buildStatus(const PlcPointValues& points) const {
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    MachineStatus status = buildStatusWith(points, m_state);
    // 故障锁存: 故障位一旦偏离正常值就进"故障态", 只有复位能退出(停止后不自动回空闲)。
    if (status.hasFault && m_state != ServiceState::FAULT) {
        setStateLocked(ServiceState::FAULT, "plc_fault");
        m_sawWashing = false;
    }

    // 洗车程序自然跑完(见过 washing=1 之后又回到 0, 且没故障): 回空闲。
    // 否则洗完这一单, 下一位顾客的启动会被"正在洗车"一直挡住(现场不会每次都补一发停止+复位)。
    if (m_state == ServiceState::RUNNING) {
        if (status.washing) {
            m_sawWashing = true;
        } else if (!status.hasFault && (m_sawWashing || isWashCompleted(points))) {
            m_sawWashing = false;
            setStateLocked(ServiceState::IDLE, "wash_finished");
        }
    }

    status.state = m_state;
    return status;
}

WashCarCheckResult WashCarStateMachine::checkCommand(const PlcPointValues& points,
                                                       const MachineStatus& status,
                                                       bool maintenance,
                                                       WashCarCommand command) const {
    WashCarCheckResult result;
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
        } else if (status.state == ServiceState::RUNNING || status.washing) {
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
        // 三态规则先于物理准入: 停止后必须先复位, 洗车中不允许重复启动
        if (status.state == ServiceState::RUNNING) {
            result.reason = "设备正在洗车中";
            result.suggestion = "请先停止";
            return result;
        }
        if (status.state == ServiceState::FAULT) {
            result.reason = "设备处于停止/故障状态";
            result.suggestion = "请先下发复位, 复位成功后才能启动";
            return result;
        }
        checkStart(points, result);
        return result;
    }
    result.reason = "未知命令";
    result.suggestion = "检查请求的命令类型";
    return result;
}

WashCarAdmission WashCarStateMachine::admit(const PlcPointValues& points, bool maintenance,
                                            WashCarCommand command, const char* commandName) {
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    WashCarAdmission admission;
    admission.previous = m_state;
    admission.current = m_state;

    // 没采到点位 = 采集没起来或 PLC 断连。宁可放行, 也不要在断连时把顾客的启动请求全拒了。
    if (points.values.empty()) {
        LOG_WARNING(std::string("[WashCarStateMachine] SM_SKIP cmd=") + commandName
                    + " reason=no_plc_points");
        return admission;
    }

    const MachineStatus status = buildStatus(points);   // 内含故障锁存(同一把递归锁)
    const WashCarCheckResult decision = checkCommand(points, status, maintenance, command);
    admission.reason = decision.reason;
    admission.suggestion = decision.suggestion;

    const bool enforce = ConfigManager::instance().stateMachine().mode == "enforce";
    if (!decision.allowed) {
        LOG_INFO(std::string("[WashCarStateMachine] SM_REJECT cmd=") + commandName
            + " enforce=" + (enforce ? "1" : "0")
            + " | " + describe(status, maintenance)
            + " | reason=\"" + decision.reason + "\""
            + " | suggestion=\"" + decision.suggestion + "\"");
        if (enforce) {
            admission.blocked = true;
            return admission;
        }
    }

    // 放行(log_only 下非法方向不迁移, 状态始终是"接受过什么命令"的记录)
    admission.transitioned = applyTransition(command);
    admission.current = m_state;
    return admission;
}

bool WashCarStateMachine::applyTransition(WashCarCommand command) {
    switch (command) {
    case WashCarCommand::START:
        if (m_state == ServiceState::IDLE) {
            m_sawWashing = false;
            setStateLocked(ServiceState::RUNNING, "start");
            return true;
        }
        return false;
    case WashCarCommand::STOP:
        if (m_state == ServiceState::RUNNING) {
            m_sawWashing = false;
            setStateLocked(ServiceState::FAULT, "stop");
            return true;
        }
        return false;
    case WashCarCommand::RESET:
        if (m_state == ServiceState::FAULT) {
            m_sawWashing = false;
            setStateLocked(ServiceState::IDLE, "reset");
            return true;
        }
        return false;
    default:
        return false;
    }
}

void WashCarStateMachine::rollback(const WashCarAdmission& admission, const char* commandName) {
    if (!admission.transitioned) return;
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (m_state != admission.current) return;   // 已被别的命令改写, 不覆盖
    LOG_WARNING(std::string("[WashCarStateMachine] SM_ROLLBACK cmd=") + commandName
                + " " + toString(admission.current) + " -> " + toString(admission.previous));
    setStateLocked(admission.previous, "rollback");
}

void WashCarStateMachine::logKeyStateChanges(const PlcPointValues& changedPoints) const {
    // 关键点位变化打印日志。changedPoints 是缓存判定为"变了"的点, 直接用, 不自己比。
    // 只打日志, 不对外暴露 —— 组变化报文时顺带打出来。
    for (const auto& name : pointTable_.keyStatePoints) {
        const SignalConfig* signal = findSignal(pointTable_, name);
        if (!signal) continue;
        int value = 0;
        if (!readPoint(changedPoints, signal->point, value)) continue;
        LOG_INFO(std::string("[WashCarStateMachine] PLC_KEY_STATE plcId=") + changedPoints.plcId
            + " point=" + signal->point + " val=" + std::to_string(value)
            + ", reason=state_change");
    }
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
    // 关键点位变化日志顺带打出来: points 就是缓存判定为"变了"的点, 不用再比一遍。
    logKeyStateChanges(points);

    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (!hasReport(signal.report, SignalReport::REPORT_MONITOR)) continue;
        if (!points.has(signal.point)) continue;   // 没变化的点不报
        int value = 0;
        readPoint(points, signal.point, value);
        report[cloudKey(signal.name)] = value;
    }
    return report;
}

// 启动准入: 逐条跑点位表的 startChecks。规则全部是数据, 机型差异只在 activeStep()。
// 顺序即优先级, 第一条不满足就返回; 文案/建议原样来自配置。
bool WashCarStateMachine::checkStart(const PlcPointValues& points, WashCarCheckResult& result) const {

    //把 reason 模板里的 `{step}` 替换成实际步号(仅对驿公里洗车机有效)
    const auto renderStep = [](std::string text, int step) {
        const std::string placeholder = "{step}";
        const std::string value = step < 0 ? std::string("?") : std::to_string(step);
        std::size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.size(), value);
            pos += value.size();
        }
        return text;
    };

    for (const auto& rule : pointTable_.startChecks) {
        if (!rule.enabled) continue;

        if (rule.kind == "activeStep") {
            const int step = activeStep(points);
            if (step < 0) {
                if (rule.missing != "reject") {
                    LOG_WARNING("[WashCarStateMachine] SM_RULE skip rule=activeStep missing=allow");
                    continue;
                }
                result.allowed = false;
                result.reason = renderStep(rule.reason, step);
                result.suggestion = rule.suggestion;
                return false;
            }
            if (step != rule.expect) {
                result.allowed = false;
                result.reason = renderStep(rule.reason, step);
                result.suggestion = rule.suggestion;
                return false;
            }
            continue;
        }

        const std::string point = signalPoint(rule.signal);
        int value = 0;
        if (point.empty() || !readPoint(points, point, value)) {
            if (rule.missing != "reject") {
                LOG_WARNING(std::string("[WashCarStateMachine] SM_RULE skip rule=") + rule.signal
                            + " missing=allow");
                continue;
            }
            result.allowed = false;
            result.reason = rule.reason;
            result.suggestion = rule.suggestion;
            return false;
        }
        if (value == rule.expect) continue;

        result.allowed = false;
        result.reason = rule.reason;
        for (const auto& alt : rule.reasonIf) {
            int altValue = 0;
            if (alt.signal.empty()) continue;
            if (readPoint(points, signalPoint(alt.signal), altValue) && altValue == alt.expect) {
                result.reason = alt.reason;
                break;
            }
        }
        result.suggestion = rule.suggestion;
        return false;
    }

    result.allowed = true;
    return true;
}

int YiGongLiStateMachine::activeStep(const PlcPointValues& points) const {
    return scanActiveStep(points);
}

HaiTianStateMachine::HaiTianStateMachine() : WashCarStateMachine("haiTian") {}

bool HaiTianStateMachine::isWashing(const PlcPointValues& points) const {
    const int step = getSignalValue(points, "stateRegister");   // D142
    // D142=10 是"洗车结束": 洗车标志可能还挂着, 但程序已经跑完 —— 不算在洗。
    // 不判掉这一步, 状态机会一直停在"运行", 下一位顾客启动会被"正在洗车"挡住。
    if (step == 10) return false;
    // 洗车标志(Y74)优先; 有些程序不驱动 Y74, 只能看工序号(D142 2..9 = 洗车中, 1 = 就绪)
    if (getSignalValue(points, "washing") != 0) return true;
    return step >= 2 && step <= 9;
}

nlohmann::json HaiTianStateMachine::buildFaultReport(const PlcPointValues& points) const {
    // 海天稠密:事件类点位全带值,故障恢复时对应点位变 0,云端据此清故障
    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (!hasReport(signal.report, SignalReport::REPORT_PUSHALARM)) continue;
        int value = 0;
        readPoint(points, signal.point, value);
        report[cloudKey(signal.name)] = value;
    }
    return report;
}

YiGongLiStateMachine::YiGongLiStateMachine() : WashCarStateMachine("yiGongLi") {}

bool HaiTianStateMachine::isWashCompleted(const PlcPointValues& points) const {
    return getSignalValue(points, "stateRegister") == 10;
}

bool YiGongLiStateMachine::isWashing(const PlcPointValues& points) const {
    const int activeStep = scanActiveStep(points);
    // S81 是"洗车完成", 与海天 D142=10 同义
    return activeStep >= 20 && activeStep < 81;
}

int YiGongLiStateMachine::scanActiveStep(const PlcPointValues& points) const {
    // STL 步任意时刻唯一 ON;采集清单是 S0~S81 有序的,多个步同时 ON(异常)时取步号最小的
    for (const auto& point : pointTable_.acquisition.sPoints) {
        if (point.empty() || point[0] != 'S') continue;
        if (points.at(point) != 0) return std::atoi(point.c_str() + 1);
    }
    return -1;
}

bool YiGongLiStateMachine::isWashCompleted(const PlcPointValues& points) const {
    return scanActiveStep(points) == 81;
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

nlohmann::json YiGongLiStateMachine::buildFaultReport(const PlcPointValues& points) const {
    // 驿公里稀疏:故障位偏离正常值才带;故障码取 D1000,有效区间 101~121
    nlohmann::json report = nlohmann::json::object();
    for (const auto& signal : pointTable_.signals) {
        if (!hasReport(signal.report, SignalReport::REPORT_PUSHALARM)) continue;
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
