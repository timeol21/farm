#include "data_layer/data_layer.h"

#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "data_layer/device_service.h"
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/fx_plc/tcp_plc_instance.h"

#include <memory>
#include <string>

namespace {

/// 海天串口的 PLC 标识是固定的：云端 payload 里的 plcId 也按这个值填
constexpr const char* kFxPlcId = "fx_plc_01";

/// 机型白名单。box.boxType 是整机唯一的"机型开关"，它同时决定三件事：
///   1. 造哪种 PLC 实例（FX 串口 / Modbus TCP）
///   2. 用哪份软元件点位表（海天 29 项 / 驿公里另一套）
///   3. 说哪套 MQTT 命令方言（CommandParser 按 boxType 选）
/// 所以这里必须显式列举支持的机型，认不出来的值直接失败。
/// 绝不能"不是驿公里就当海天"——那样驿公里机器会拿海天的点位表去读 Modbus，
/// 现象是"所有点值恒为 0 / 满屏故障"，而日志里看不出任何异常。
bool isSupportedBoxType(const std::string& t) {
    return t == "haiTian" || t == "yiGongLi";
}

}  // namespace

DataLayer::DataLayer() = default;

DataLayer::~DataLayer() {
    stop();
}

bool DataLayer::init() {
    auto& cfg = ConfigManager::instance();
    const auto& box = cfg.box();

    // ---------- 1) 机型匹配：配置 -> 走哪条 PLC 线 ----------
    if (!isSupportedBoxType(box.boxType)) {
        LOG_ERROR("[DataLayer] 机型不匹配: box.boxType=\"" + box.boxType
                  + "\" 不是支持的值 (haiTian / yiGongLi)，请检查 config.json 的 box 段");
        return false;
    }

    const bool isYiGongLi = (box.boxType == "yiGongLi");
    LOG_INFO("[DataLayer] 机型匹配: box.boxType=" + box.boxType + " -> "
             + (isYiGongLi ? "驿公里 / Modbus TCP" : "海天 / 三菱 FX 串口"));

    // ---------- 2) 点位表：是否真的用了 config.json 里的 machine 段 ----------
    const auto& table = cfg.machine();
    LOG_INFO("[DataLayer] 点位表: "
             + std::string(cfg.machineFromConfig() ? "来自 config.json 的 machine 段"
                                                   : "来自内置默认表 default_point")
             + ", signals=" + std::to_string(table.signals.size())
             + ", services=" + std::to_string(table.services.size()));
    if (!cfg.machineFromConfig()) {
        // 回退内置表不是错误（现场没配 machine 段也能跑），但必须留痕：
        // 一旦实机点位与内置表不一致，这条 WARNING 是唯一线索。
        LOG_WARNING("[DataLayer] machine 段没有可用配置，已回退内置点位表，"
                    "现场需确认点位定义与实机一致");
    }

    // ---------- 3) 造 PLC 实例 ----------
    plcId_ = isYiGongLi ? cfg.plcTcp().plcId : std::string(kFxPlcId);

    if (!box.plcEnabled) {
        LOG_WARNING("[DataLayer] box.plcEnabled=false，本轮不注册 PLC（调试模式）；"
                    "业务层读到的点值将全部为空");
    } else if (isYiGongLi) {
        const auto& tcp = cfg.plcTcp();
        fxPlcSet_.addPlc(std::make_unique<TcpPlcInstance>(
            tcp.plcId, tcp.host, static_cast<uint16_t>(tcp.port),
            static_cast<uint8_t>(tcp.unitId), tcp.timeoutMs));
        plcRegistered_ = true;
        LOG_INFO("[DataLayer] PLC 实例: " + tcp.plcId + " Modbus TCP " + tcp.host + ":"
                 + std::to_string(tcp.port) + ", unitId=" + std::to_string(tcp.unitId)
                 + ", 超时=" + std::to_string(tcp.timeoutMs) + "ms");
    } else {
        const auto& ser = cfg.plcSerial();
        FxPlcDevice fxDevice(kFxPlcId, ser.port, ser.baudRate, ser.dataBits,
                             ser.parity == "even", ser.stopBits,
                             ser.timeoutMs, ser.frameDelayMs);
        fxPlcSet_.addPlc(std::make_unique<FxPlcInstance>(fxDevice));
        plcRegistered_ = true;
        LOG_INFO("[DataLayer] PLC 实例: " + std::string(kFxPlcId) + " FX 串口 " + ser.port + ", "
                 + std::to_string(ser.baudRate) + " " + std::to_string(ser.dataBits)
                 + (ser.parity == "even" ? "E" : "N") + std::to_string(ser.stopBits)
                 + ", 超时=" + std::to_string(ser.timeoutMs) + "ms"
                 + ", 帧间隔=" + std::to_string(ser.frameDelayMs) + "ms");
    }

    // ---------- 4) 采集服务（线程在 start() 里起）----------
    device_ = std::make_unique<DeviceService>(table, fxPlcSet_);
    LOG_INFO("[DataLayer] 采集服务就绪, 周期=" + std::to_string(device_->intervalMs()) + "ms");
    return true;
}

void DataLayer::start() {
    if (device_) {
        device_->start();
    }
}

void DataLayer::stop() {
    if (device_) {
        device_->stop();
    }
}

IDeviceService& DataLayer::device() {
    return *device_;
}
