// washcar_test.cpp — 洗车服务自检。
// 覆盖两块:
//   1. 洗车状态机(点位表驱动的物理判定 / 三态迁移 / 命令准入与回滚 / 守卫开关)
//   2. 洗车服务对外方法(海天四条命令 + 故障 + 写点失败回滚 + 维修互斥 + 单点控制
//      + HTTP 层契约 + 三条对外口径基线)
//
// 2026-10-26 追加: 三个对外口径的回归基线
//   口径1 单点复位(point/reset) = 整机复位, 与 washcar/reset 等价, 不受维修模式约束
//   口径2 维修模式切换: 业务层回执带真实状态; HTTP 层回执现状是硬编码 success
//   口径3 readData 按机型点位表分流; 现状是硬编码海天点位
// 基线断言都指向"现状", 口径变更时按注释同步翻 —— 改行为前测试先红, 这是回归闸门的用法。
//
// 目标规格开关: 三个口径的 C++ 实现还没落地, 所以这里锁现状(false)。
// 拿到改动授权后把 kEnableTargetSpec 翻成 true, 目标规格断言会先红 ——
// 红了就是"该改 C++ 了"的信号, 改完再翻绿。这就是把规格留在仓里的可执行形式。
//
// 用法: ./washcar_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "business_layer/timer.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/washcar/washcar_state_machine.h"
#include "common/config/config_manager.h"
#include "common/config/default_point.h"
#include "presentation_layer/http_command.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

using namespace washcar::cfg;

namespace {

int g_pass = 0;
int g_fail = 0;

void expect(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::cout << "  [FAIL] " << what << std::endl;
}

constexpr bool kEnableTargetSpec = false;

// ---------- 状态机用的点位快照 ----------

// 海天:一台正常待机的洗车机。启动准入 4 条规则涉及的点都放成"通过"。
PlcPointValues haiTianBaseline() {
    return PlcPointValues{{{"Y74", 0}, {"D142", 1}, {"Y50", 0}, {"Y51", 0}, {"Y52", 1},
                           {"M187", 1}, {"M1", 1}, {"X44", 0}, {"S0", 0},
                           {"D28", 0}, {"X50", 1}, {"M257", 0}, {"M259", 0}, {"M381", 0},
                           {"X62", 1}, {"X63", 1}, {"X46", 1}, {"X3", 0}}};
}

PlcPointValues withValue(const char* point, int value) {
    PlcPointValues points = haiTianBaseline();
    points.values[point] = value;
    return points;
}

// 驿公里:待机 S5 + 车位到位
PlcPointValues yiGongLiBaseline() {
    return PlcPointValues{{{"S5", 1}, {"M510", 0}, {"M282", 1}, {"D1000", 0},
                           {"M147", 0}, {"M148", 0}, {"M169", 0}}};
}

// ---------- 洗车服务用的假设备服务 ----------

// 假设备服务: 一台海天 PLC 的点位快照 + 可配置的写点位结果
class FakeDeviceService : public IDeviceService {
public:
    // 一次写操作的记录: 写的是哪个区、哪个号、什么值。
    // 只记数量看不出"写了哪个点", 口径1 要断言点/复位写的是哪两个软元件。
    struct WriteOp {
        char area;   // 'M' / 'X'
        int addr;
        bool value;
    };

    std::vector<FxPlcStatus> plcs;
    bool writeOk = true;
    int writes = 0;
    std::vector<WriteOp> fxWrites;

    FakeDeviceService() {
        FxPlcStatus plc("fx_plc_01");
        plc.updateDRegister(142, 1);
        plc.updateMBit(187, true);   // 车辆停放到位
        plc.updateMBit(1, true);     // 启动前置条件
        plc.updateXBit(62, true);    // 左防撞杆
        plc.updateXBit(63, true);    // 右防撞杆
        plc.updateXBit(46, true);    // 车辆高度
        plc.updateXBit(50, true);    // 水位正常(低电平有效)
        plc.updateXBit(44, false);
        plc.updateSBit(0, false);
        plc.updateDRegister(28, 0);
        plcs.push_back(plc);
    }

    void setWashing(bool on) { plcs[0].updateYBit(74, on); }
    void setFault(int code) { plcs[0].updateDRegister(28, static_cast<uint16_t>(code)); }

    BoxDeviceStatus viewAllDeviceStatus() override {
        std::vector<CameraStatus> cameras;
        std::vector<TempHumidSensorStatus> sensors;
        std::vector<DoorLockStatus> locks;
        return BoxDeviceStatus(cameras, sensors, locks, plcs);
    }
    CameraRealTimeFrame getCameraRealTimeFrame(const CameraInfo&) override { return CameraRealTimeFrame(); }
    CameraHistoryVideo viewCameraHistoryVideo(const CameraInfo&) override { return CameraHistoryVideo(); }
    bool forceFxM(const std::string&, int addr, bool value) override {
        fxWrites.push_back({'M', addr, value});
        ++writes;
        return writeOk;
    }
    bool forceFxX(const std::string&, int addr, bool value) override {
        fxWrites.push_back({'X', addr, value});
        ++writes;
        return writeOk;
    }
    std::vector<FxPlcStatus> getChangedFxPlcStatus() override { return {}; }
    void clearChangedFxPlcStatus(const std::vector<std::string>&) override {}
    void acquisitionOnce() override {}
    bool readPointNow(const std::string&, const std::string&, int&, std::string&) override { return false; }
};

WashCarOperation makeOperation(const std::string& source = "http") {
    json body;
    body["boxNo"] = "123456";
    body["plcId"] = "fx_plc_01";
    body["reqSource"] = source;
    return WashCarOperation(body);
}

// 带点位的操作(单点控制族要带 point; point/reset 目前只认 "all")
WashCarOperation makePointOperation(const std::string& point, const std::string& source = "http") {
    json body;
    body["boxNo"] = "123456";
    body["plcId"] = "fx_plc_01";
    body["reqSource"] = source;
    body["point"] = point;
    return WashCarOperation(body);
}

// 是否写过 M<addr>=<value>
bool wroteM(const FakeDeviceService& device, int addr, bool value) {
    for (const auto& op : device.fxWrites) {
        if (op.area == 'M' && op.addr == addr && op.value == value) return true;
    }
    return false;
}

// 维修标志是落盘状态: 上一轮跑测试留下的残留会改变这一轮的判定,
// 某次中途失败还会让后面每一轮都跟着错。涉及维修的用例先归零, 测试才幂等。
void resetMaintenanceFlag() {
    const std::string root = ConfigManager::instance().box().storageRoot;
    if (!root.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
    }
    expect(WashCarStateMachine::saveMaintenanceFlag(root, false), "前置: 维修标志归零");
}

// HTTP 层用的假大厅: 只保留要验的命令入口, 其余一律回"不支持"。
// 业务层的判定由下面的真实洗车服务覆盖, 这里只盯 HTTP 层自己那层壳:
// 回执格式、状态码、点位选择、reqSource 契约。
class StubLobby : public ILobbyService {
public:
    bool changeModeFails = false;
    std::string changeModeMessage = "maintenance on";
    std::string lastReqSource;
    std::string lastPoint;

    std::vector<FxPlcStatus> plcs;

    StubLobby() {
        FxPlcStatus plc("fx_plc_01");
        plc.updateDRegister(142, 3);
        plc.updateYBit(74, true);
        plc.updateMBit(1, true);
        plc.updateMBit(187, true);
        plcs.push_back(plc);
    }

    std::vector<FxPlcStatus> viewPlcStatus() override { return plcs; }
    bool isWashCarWashing() override { return false; }
    bool isMqttConnected() override { return true; }

    LobbyResult<bool> changeMode(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        if (changeModeFails) {
            return LobbyResult<bool>::Error(ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED,
                                           changeModeMessage);
        }
        // 业务层回执带真实维修状态。HTTP 层现状不透传它(硬编码 success),
        // 所以基线断言查 body 还是 success, 只有目标规格才要求透传。
        LobbyResult<bool> ok = LobbyResult<bool>::Ok(true);
        ok.message = changeModeMessage;
        return ok;
    }

    LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        WashCarOperationResult result("all", ErrorCode::Code::SUCCESS, "read ok");
        result.setCurrentStep(3);
        return LobbyResult<WashCarOperationResult>::Ok(result);
    }

    LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        return LobbyResult<WashCarOperationResult>::Ok(
            WashCarOperationResult("all", ErrorCode::Code::SUCCESS, "success"));
    }

    LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        return LobbyResult<WashCarOperationResult>::Ok(
            WashCarOperationResult("all", ErrorCode::Code::SUCCESS, "stopped"));
    }

    LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        return LobbyResult<WashCarOperationResult>::Ok(
            WashCarOperationResult("all", ErrorCode::Code::SUCCESS, "reset success"));
    }

    LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation& operation) override {
        lastReqSource = operation.getReqSource();
        lastPoint = operation.getPoint();
        return LobbyResult<WashCarOperationResult>::Ok(
            WashCarOperationResult("all", ErrorCode::Code::SUCCESS, "reset success"));
    }

    // ---- 以下不参与本测试, 一律回"不支持" ----
    LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery&) override {
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery&) override {
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery&) override {
        return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo&) override {
        return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation&) override {
        return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> abortWashCar(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> stopWork(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> stopService(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> resetSafe(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string&) override { return nullptr; }
    bool checkDetectionBeforeWashCar(const std::string&) override { return true; }
    void onPointValueChanged(const std::string&, const std::string&, bool) override {}
};

}  // namespace

int main() {
    // =====================================================================
    // 1. 洗车状态机
    // =====================================================================
    ConfigManager::instance().applyDefaults();
    {
        std::string error;
        const auto& pointTable = ConfigManager::instance().machine();
        expect(pointTable.machineType == "haiTian", "1. 默认点位表是海天");
        expect(validatePointTable(pointTable, error), "1. 海天点位表自检通过: " + error);
    }

    // ===== 1.1 物理判定(状态由命令驱动, 这里只看 washing/hasFault/allowStart) =====
    {
        HaiTianStateMachine machine;
        auto status = machine.buildStatus(haiTianBaseline());
        expect(status.state == ServiceState::IDLE, "1.1 未洗车未故障 → 空闲");
        expect(!status.washing && !status.hasFault && !status.needsReset, "1.1 基准状态三个标志都 false");
        expect(status.allowStart, "1.1 基准状态允许启动");

        status = machine.buildStatus(withValue("Y74", 1));
        expect(status.washing, "1.1 Y74=1 → washing 标志 true");
        expect(status.state == ServiceState::IDLE, "1.1 PLC 在洗车但状态机没受理启动 → 仍是空闲(状态由命令迁移)");

        auto both = withValue("D28", 60);
        both.values["Y74"] = 1;
        status = machine.buildStatus(both);
        expect(status.hasFault && status.state == ServiceState::FAULT, "1.1 故障位偏离 → 锁存成故障态");

        // 需要先复位(X44 / S0):不算故障, 只影响复位命令
        HaiTianStateMachine fresh;
        status = fresh.buildStatus(withValue("X44", 1));
        expect(status.needsReset && status.state == ServiceState::IDLE && !status.hasFault,
               "1.1 X44=1 → needsReset,不改变状态,不算故障");
        expect(fresh.buildStatus(withValue("S0", 1)).needsReset, "1.1 S0=1 → needsReset");
    }

    // ===== 1.2 海天启动准入 4 条(物理条件, 交给子类) =====
    {
        HaiTianStateMachine machine;
        auto status = machine.buildStatus(withValue("M187", 0));
        expect(!status.allowStart, "1.2 M187=0 → 不允许启动");
        expect(status.blockReason.find("washPosition incorrect") != std::string::npos,
               "1.2 M187=0 理由是 washPosition incorrect");

        auto advanced = withValue("M187", 0);
        advanced.values["X3"] = 1;
        expect(machine.buildStatus(advanced).blockReason.find("parking advanced") != std::string::npos,
               "1.2 M187=0 且 X3=1 理由是 parking advanced");

        expect(machine.buildStatus(withValue("M1", 0)).blockReason.find("startCondition not") != std::string::npos,
               "1.2 M1=0 → 理由 startCondition not");
        expect(machine.buildStatus(withValue("X62", 0)).blockReason.find("anti-collision") != std::string::npos,
               "1.2 X62=0 → 理由撞了防撞杆");
        expect(!machine.buildStatus(withValue("X63", 0)).allowStart, "1.2 X63=0 → 不允许启动");
        expect(machine.buildStatus(withValue("X46", 0)).blockReason.find("vehicle too tall") != std::string::npos,
               "1.2 X46=0 → 理由车辆过高");

        auto missing = haiTianBaseline();
        missing.values.erase("M187");
        missing.values.erase("M1");
        missing.values.erase("X46");
        expect(machine.buildStatus(missing).allowStart, "1.2 海天:读不到前置点位 → 放行");
    }

    // ===== 1.3 三态迁移: 空闲 -启动-> 运行 -停止-> 故障 -复位-> 空闲 =====
    {
        HaiTianStateMachine machine;
        expect(machine.state() == ServiceState::IDLE, "1.3 初始为空闲");

        auto start = machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        expect(!start.blocked && start.transitioned, "1.3 启动被受理并占位成运行");
        expect(machine.state() == ServiceState::RUNNING, "1.3 启动后状态=运行");

        auto again = machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        expect(again.blocked, "1.3 运行中重复启动被拒");
        expect(again.reason.find("正在洗车") != std::string::npos, "1.3 重复启动理由=正在洗车");

        auto stop = machine.admit(withValue("Y74", 1), false, WashCarCommand::STOP, "stopWashCar");
        expect(!stop.blocked && stop.transitioned, "1.3 停止被受理");
        expect(machine.state() == ServiceState::FAULT, "1.3 停止后状态=故障(待复位)");

        auto afterStop = machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        expect(afterStop.blocked, "1.3 停止后不复位不能启动");
        expect(afterStop.reason.find("停止/故障") != std::string::npos, "1.3 拒绝理由指向需要复位");

        auto reset = machine.admit(haiTianBaseline(), false, WashCarCommand::RESET, "resetWashCar");
        expect(!reset.blocked && reset.transitioned, "1.3 复位被受理");
        expect(machine.state() == ServiceState::IDLE, "1.3 复位后状态=空闲");

        expect(!machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar").blocked,
               "1.3 复位后可以再次启动");
    }

    // ===== 1.4 洗车自然结束: 回空闲, 不用现场补一发停止+复位 =====
    {
        HaiTianStateMachine machine;
        machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        machine.buildStatus(withValue("Y74", 1));       // PLC 真的开始洗
        expect(machine.state() == ServiceState::RUNNING, "1.4 洗车中仍是运行");
        machine.buildStatus(haiTianBaseline());         // 洗完了(Y74 回 0)
        expect(machine.state() == ServiceState::IDLE, "1.4 洗车自然结束 → 回空闲");

        // 有些版程序: D142 已经到 10, Y74 还挂着 1 —— 也要判成"洗完"
        HaiTianStateMachine ladder;
        ladder.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        ladder.buildStatus(withValue("Y74", 1));
        auto atTen = haiTianBaseline();
        atTen.values["Y74"] = 1;
        atTen.values["D142"] = 10;
        expect(!ladder.buildStatus(atTen).washing, "1.4 D142=10 且 Y74=1 → 不算在洗");
        expect(ladder.state() == ServiceState::IDLE, "1.4 D142 到 10 → 回空闲");

        // 采集漏掉中间步骤, 只看到"D142 直接跳到 10": 也要能收回空闲
        HaiTianStateMachine jumped;
        jumped.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        auto jumpToTen = haiTianBaseline();
        jumpToTen.values["D142"] = 10;
        expect(jumped.buildStatus(jumpToTen).state == ServiceState::IDLE,
               "1.4 没采到 2~9、直接看到 D142=10 → 也回空闲");

        // 不驱动 Y74 的程序: 只看 D142 也要判出"洗车中"
        auto stepOnly = haiTianBaseline();
        stepOnly.values["D142"] = 3;
        expect(HaiTianStateMachine().buildStatus(stepOnly).washing, "1.4 D142=3 且 Y74=0 → 洗车中");
        auto ready = haiTianBaseline();   // D142=1 就绪
        expect(!HaiTianStateMachine().buildStatus(ready).washing, "1.4 D142=1 就绪 → 不算洗车中");
        expect(!machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar").blocked,
               "1.4 洗完后下一位可以直接启动");
    }

    // ===== 1.5 幂等: 空闲时停止/复位都放行且不迁移 =====
    {
        HaiTianStateMachine machine;
        auto stop = machine.admit(haiTianBaseline(), false, WashCarCommand::STOP, "stopWashCar");
        expect(!stop.blocked && !stop.transitioned && machine.state() == ServiceState::IDLE,
               "1.5 空闲时停止: 放行但不迁移");
        auto reset = machine.admit(haiTianBaseline(), false, WashCarCommand::RESET, "resetWashCar");
        expect(!reset.blocked && !reset.transitioned && machine.state() == ServiceState::IDLE,
               "1.5 空闲时复位: 放行但不迁移");
    }

    // ===== 1.6 PLC 故障锁存: 进故障态后只能复位 =====
    {
        HaiTianStateMachine machine;
        machine.buildStatus(withValue("D28", 68));
        expect(machine.state() == ServiceState::FAULT, "1.6 故障位出现 → 锁存故障");
        expect(machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar").blocked,
               "1.6 故障态不允许启动(即便点位已恢复)");
        expect(!machine.admit(haiTianBaseline(), false, WashCarCommand::RESET, "resetWashCar").blocked,
               "1.6 故障态允许复位");
        expect(machine.state() == ServiceState::IDLE, "1.6 复位后回到空闲");
    }

    // ===== 1.7 PLC 动作失败回滚 =====
    {
        HaiTianStateMachine machine;
        auto start = machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        expect(machine.state() == ServiceState::RUNNING, "1.7 启动占位");
        machine.rollback(start, "startWashCar");
        expect(machine.state() == ServiceState::IDLE, "1.7 写点失败回滚后回到空闲");

        // 回到运行态再验停止回滚 —— 停止回滚要把状态放回运行, 不是空闲
        machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        auto stop = machine.admit(haiTianBaseline(), false, WashCarCommand::STOP, "stopWashCar");
        machine.rollback(stop, "stopWashCar");
        expect(machine.state() == ServiceState::RUNNING, "1.7 停止失败回滚后仍在运行");
    }

    // ===== 1.8 维修命令: 洗车中禁止, 其余允许 =====
    {
        HaiTianStateMachine machine;
        expect(!machine.admit(haiTianBaseline(), false, WashCarCommand::ENTER_MAINTENANCE, "maintain").blocked,
               "1.8 空闲可进维修");
        machine.admit(haiTianBaseline(), true, WashCarCommand::EXIT_MAINTENANCE, "maintain");
        expect(machine.admit(haiTianBaseline(), true, WashCarCommand::ENTER_MAINTENANCE, "maintain").blocked,
               "1.8 已在维修时重复进入被拒");

        machine.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar");
        auto running = machine.admit(withValue("Y74", 1), false, WashCarCommand::ENTER_MAINTENANCE, "maintain");
        expect(running.blocked && running.reason.find("洗车中") != std::string::npos,
               "1.8 运行中禁止进维修");
    }

    // ===== 1.9 守卫开关: enforce 真拒, log_only 只记日志 =====
    {
        ConfigManager::instance().setStateMachineModeForTest("log_only");
        HaiTianStateMachine logOnly;
        auto allowed = logOnly.admit(withValue("M1", 0), false, WashCarCommand::START, "startWashCar");
        expect(!allowed.blocked, "1.9 log_only: 本应拒绝的启动照常执行");
        expect(logOnly.state() == ServiceState::RUNNING, "1.9 log_only: 执行的命令照样迁移状态");

        ConfigManager::instance().setStateMachineModeForTest("enforce");
        HaiTianStateMachine enforce;
        auto blocked = enforce.admit(withValue("M1", 0), false, WashCarCommand::START, "startWashCar");
        expect(blocked.blocked, "1.9 enforce: 物理准入不过真拒");
        expect(enforce.state() == ServiceState::IDLE, "1.9 enforce: 被拒不迁移");
        expect(!enforce.admit(haiTianBaseline(), false, WashCarCommand::START, "startWashCar").blocked,
               "1.9 enforce: 条件满足照样放行");
    }

    // ===== 1.10 没点位: 不判定也不迁移 =====
    {
        HaiTianStateMachine machine;
        const PlcPointValues empty;
        auto admission = machine.admit(empty, false, WashCarCommand::START, "startWashCar");
        expect(!admission.blocked && !admission.transitioned, "1.10 没点位时不判定(放行)也不迁移");
        expect(machine.buildStatus(empty).state == ServiceState::IDLE, "1.10 没点位时状态保持空闲");
        expect(machine.describeNow(empty, false).find("未就绪") != std::string::npos,
               "1.10 没点位时描述返回未就绪");
    }

    // ===== 1.11 驿公里: 同样三态, 只验对称性 =====
    {
        YiGongLiStateMachine machine;
        auto status = machine.buildStatus(yiGongLiBaseline());
        expect(status.state == ServiceState::IDLE && status.allowStart, "1.11 驿公里待机 → 空闲且允许启动");

        auto washing = yiGongLiBaseline();
        washing.values.erase("S5");
        washing.values["S20"] = 1;
        status = machine.buildStatus(washing);
        expect(status.washing, "1.11 驿公里 S20 → washing");

        auto start = machine.admit(yiGongLiBaseline(), false, WashCarCommand::START, "startWashCar");
        expect(!start.blocked && machine.state() == ServiceState::RUNNING, "1.11 驿公里启动 → 运行");
        auto stop = machine.admit(washing, false, WashCarCommand::STOP, "stopWashCar");
        expect(!stop.blocked && machine.state() == ServiceState::FAULT, "1.11 驿公里停止 → 故障");
        expect(machine.admit(yiGongLiBaseline(), false, WashCarCommand::START, "startWashCar").blocked,
               "1.11 驿公里停止后必须先复位");
    }

    // ===== 1.12 启动准入由点位表 startChecks 驱动 =====
    {
        // 1.12.1 内置默认: 规则条数 + reasonIf(X3=1 换文案)
        {
            HaiTianStateMachine machine;
            auto advanced = withValue("M187", 0);
            advanced.values["X3"] = 1;
            WashCarCheckResult result;
            expect(!machine.checkStart(advanced, result), "1.12.1 M187=0 拒绝启动");
            expect(result.reason == "parking advanced", "1.12.1 reasonIf 命中 X3=1 换文案");
        }

        // 1.12.2 把 washPosition 的 missing 改成 reject: 读不到点就拒
        {
            auto table = default_point::getPointTable("haiTian");
            for (auto& rule : table.startChecks) {
                if (rule.signal == "washPosition") rule.missing = "reject";
            }
            ConfigManager::instance().setActiveMachineForTest(table);

            HaiTianStateMachine machine;
            auto missing = haiTianBaseline();
            missing.values.erase("M187");
            WashCarCheckResult result;
            expect(!machine.checkStart(missing, result), "1.12.2 missing=reject: 缺 M187 → 拒绝");
            expect(result.reason == "washPosition incorrect", "1.12.2 拒绝理由来自配置");
        }

        // 1.12.3 改期望值: startCondition 期望 0 → M1=1 拒, M1=0 过
        {
            auto table = default_point::getPointTable("haiTian");
            for (auto& rule : table.startChecks) {
                if (rule.signal == "startCondition") rule.expect = 0;
            }
            ConfigManager::instance().setActiveMachineForTest(table);

            HaiTianStateMachine machine;
            WashCarCheckResult rejected;
            expect(!machine.checkStart(haiTianBaseline(), rejected), "1.12.3 期望 0 时 M1=1 被拒");
            WashCarCheckResult allowed;
            expect(machine.checkStart(withValue("M1", 0), allowed), "1.12.3 期望 0 时 M1=0 放行");
        }

        // 1.12.4 停用一条内置规则
        {
            auto table = default_point::getPointTable("haiTian");
            for (auto& rule : table.startChecks) {
                if (rule.signal == "washPosition") rule.enabled = false;
            }
            ConfigManager::instance().setActiveMachineForTest(table);

            HaiTianStateMachine machine;
            WashCarCheckResult result;
            expect(machine.checkStart(withValue("M187", 0), result),
                   "1.12.4 停用 washPosition 后 M187=0 也允许启动");
        }

        // 1.12.5 驿公里 activeStep 规则: 文案带当前步号
        {
            ConfigManager::instance().setActiveMachineForTest(
                default_point::getPointTable("yiGongLi"));
            YiGongLiStateMachine machine;
            auto notStandby = yiGongLiBaseline();
            notStandby.values.erase("S5");
            notStandby.values["S30"] = 1;
            WashCarCheckResult result;
            expect(!machine.checkStart(notStandby, result), "1.12.5 非待机步拒绝启动");
            expect(result.reason == "not in standby (S30)", "1.12.5 activeStep 文案替换 {step}");
        }

        // 恢复默认表, 不影响其它用例
        ConfigManager::instance().setActiveMachineForTest(
            default_point::getPointTable("haiTian"));
    }

    // =====================================================================
    // 2. 洗车服务对外方法(海天)
    // =====================================================================
    ConfigManager::instance().applyDefaults();
    ConfigManager::instance().setStateMachineModeForTest("enforce");

    // ===== 2.1 四条命令走完一个完整生命周期 =====
    {
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.1 启动成功");

        WashCarOperationResult second = service.startWashCar(op);
        expect(second.getCode() == ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED, "2.1 运行中重复启动被拒");
        expect(second.getMessage().find("正在洗车") != std::string::npos, "2.1 重复启动理由=正在洗车");

        expect(service.stopWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.1 停止成功");
        WashCarOperationResult afterStop = service.startWashCar(op);
        expect(afterStop.getCode() == ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED, "2.1 停止后不复位不能启动");
        expect(afterStop.getMessage().find("复位") != std::string::npos, "2.1 拒绝理由指向复位");

        expect(service.resetWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.1 复位成功");
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.1 复位后可以再启动");
    }

    // ===== 2.2 空闲时停止幂等, 不会把状态打成故障 =====
    {
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        expect(service.stopWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.2 空闲时停止放行");
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.2 空闲停止后仍能启动");
    }

    // ===== 2.3 写点位失败要回滚(失败那一发不能占着"运行") =====
    {
        FakeDeviceService device;
        device.writeOk = false;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        WashCarOperationResult failed = service.startWashCar(op);
        expect(failed.getCode() == ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED, "2.3 PLC 拒绝时启动失败");
        device.writeOk = true;
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.3 失败后回滚, 可以重试启动");
    }

    // ===== 2.4 洗车自然结束: 不用复位也能接下一单 =====
    {
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.4 启动成功");
        device.setWashing(true);
        service.getWashCarStatus(op);          // 观测到"正在洗"
        device.setWashing(false);
        service.getWashCarStatus(op);          // 洗完
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.4 洗完可直接接下一单");
    }

    // ===== 2.5 PLC 故障: 启动被拒, 复位后恢复 =====
    {
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        device.setFault(60);
        WashCarOperationResult faultQuery = service.getWashCarStatus(op);   // 故障锁存
        expect(faultQuery.getCode() == ErrorCode::Code::SUCCESS,
               "2.5 故障态查询照样放行(查询是恢复手段的一部分, 不能被故障挡住)");
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED,
               "2.5 故障态启动被拒");
        expect(service.stopWashCar(op).getCode() == ErrorCode::Code::SUCCESS,
               "2.5 故障态停止放行(故障时也得能停)");
        device.setFault(0);
        expect(service.resetWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.5 故障态可以复位");
        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.5 复位后可以启动");
    }

    // ===== 2.6 维修命令: 洗车中禁止; 云端来源禁止 =====
    {
        resetMaintenanceFlag();
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        expect(service.maintain(makeOperation("mqtt")).getCode() == ErrorCode::Code::AUTH_PERMISSION_DENIED,
               "2.6 云端不能切维修模式");
        expect(service.maintain(op).getCode() == ErrorCode::Code::SUCCESS, "2.6 空闲可进维修模式");
        expect(service.maintain(op).getCode() == ErrorCode::Code::SUCCESS, "2.6 再点一次退出维修模式");

        expect(service.startWashCar(op).getCode() == ErrorCode::Code::SUCCESS, "2.6 非维修状态下本地可启动");
        WashCarOperationResult blocked = service.maintain(op);
        expect(blocked.getCode() == ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED, "2.6 洗车中禁止切维修");
        expect(blocked.getMessage().find("洗车中") != std::string::npos, "2.6 拒绝理由=洗车中");
    }

    // ===== 2.7 口径1基线: 单点复位 = 整机复位, 不受维修模式约束 =====
    // 口径(2026-10-26 定): point/reset 写的是 M0=0 + M100=1, 与 washcar/reset 等价,
    // 属于"整机复位"而不是"单点控制", 所以不受"单点控制只允许在维修模式"约束。
    // 状态机里 RESET 永远放行(复位是恢复手段, 不该被挡), point/reset 跟它同一条路。
    {
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        WashCarOperationResult reset = service.pointReset(makePointOperation("all"));
        expect(reset.getCode() == ErrorCode::Code::SUCCESS, "2.7 口径1: 非维修模式下 point/reset 放行");
        expect(wroteM(device, 0, false), "2.7 口径1: point/reset 先写 M0=0(清停止标志)");
        expect(wroteM(device, 100, true), "2.7 口径1: point/reset 再写 M100=1(复位)");

        // point 不是 "all" 时不执行(现状: 报"未实现")
        WashCarOperationResult other = service.pointReset(makePointOperation("M1"));
        expect(other.getCode() == ErrorCode::Code::WASHCAR_POINT_RESET_UNSUPPORTED,
               "2.7 口径1: point/reset 只认 all, 其它点位报未实现");
    }
    {
        // 跟 washcar/reset 写的是同一组软元件 —— 两者语义等价, 这是"归整机复位"的口径依据
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        service.resetWashCar(op);
        expect(wroteM(device, 0, false), "2.7 口径1: washcar/reset 同样先写 M0=0");
        expect(wroteM(device, 100, true), "2.7 口径1: washcar/reset 同样写 M100=1");
    }
    {
        // 洗车中也放行: 与 washcar/reset 同一语义(RESET 永远放行)
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        service.startWashCar(op);
        device.setWashing(true);
        service.getWashCarStatus(op);
        WashCarOperationResult duringWash = service.pointReset(makePointOperation("all"));
        expect(duringWash.getCode() == ErrorCode::Code::SUCCESS,
               "2.7 口径1: 洗车中 point/reset 放行(同 RESET 语义)");
    }

    // ===== 2.8 口径2基线: 维修模式切换的业务层回执带真实状态 =====
    {
        resetMaintenanceFlag();
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        WashCarOperationResult on = service.maintain(op);
        expect(on.getCode() == ErrorCode::Code::SUCCESS, "2.8 口径2: 空闲可进维修模式");
        expect(on.getMessage() == "maintenance on", "2.8 口径2: 业务层回执带真实状态=maintenance on");

        WashCarOperationResult off = service.maintain(op);
        expect(off.getCode() == ErrorCode::Code::SUCCESS, "2.8 口径2: 再调一次退出维修模式");
        expect(off.getMessage() == "maintenance off", "2.8 口径2: 业务层回执带真实状态=maintenance off");
    }

    // ===== 2.9 口径2/3基线 + HTTP 层契约 =====
    {
        StubLobby lobby;
        HTTPCommandController controller(lobby);

        // --- 口径2: changeMode 的回执 ---
        HttpResponse r = controller.handleHttp("/93/11/washcar/changeMode", "{}");
        expect(r.status == 200, "2.9 口径2: changeMode 成功回 200");
        if (kEnableTargetSpec) {
            expect(r.body.find("\"message\":\"maintenance on\"") != std::string::npos,
                   "2.9 口径2目标: changeMode 回执带真实状态 maintenance on");
            expect(r.body.find("\"maintaining\":true") != std::string::npos,
                   "2.9 口径2目标: changeMode 回执带 maintaining 字段");
        } else {
            expect(r.body.find("\"message\":\"success\"") != std::string::npos,
                   "2.9 口径2基线: changeMode 回执固定 message=success(目标: 回真实状态, 口径变更时改这里)");
            expect(r.body.find("maintaining") == std::string::npos,
                   "2.9 口径2基线: changeMode 回执不带 maintaining 字段(目标: 带, 口径变更时改这里)");
        }

        lobby.changeModeFails = true;
        lobby.changeModeMessage = "洗车中不能进入维修模式";
        r = controller.handleHttp("/93/11/washcar/changeMode", "{}");
        if (kEnableTargetSpec) {
            expect(r.status == 409, "2.9 口径2目标: 业务拒绝回 HTTP 409");
        } else {
            expect(r.status == 500, "2.9 口径2基线: 业务拒绝回 HTTP 500(目标: 409/403, 口径变更时改这里)");
        }
        expect(r.body.find("洗车中不能进入维修模式") != std::string::npos,
               "2.9 口径2: 失败回执带业务原因, 不吞文案");
        lobby.changeModeFails = false;

        // --- 口径3: readData 按机型点位表分流 ---
        r = controller.handleHttp("/93/11/washcar/readData", "{}");
        expect(r.body.find("\"d142\"") != std::string::npos, "2.9 口径3: 海天下 readData 回 d142");
        expect(r.body.find("\"y74\"") != std::string::npos, "2.9 口径3: 海天下 readData 回 y74");

        ConfigManager::instance().setActiveMachineForTest(ConfigManager::instance().machine("yiGongLi"));
        r = controller.handleHttp("/93/11/washcar/readData", "{}");
        if (kEnableTargetSpec) {
            expect(r.body.find("\"d142\"") == std::string::npos,
                   "2.9 口径3目标: 驿公里下 readData 不再回海天点位 d142");
        } else {
            expect(r.body.find("\"d142\"") != std::string::npos,
                   "2.9 口径3基线: 驿公里下 readData 仍回海天点位 d142(目标: 按点位表分流, 口径变更时改这里)");
        }
        ConfigManager::instance().setActiveMachineForTest(ConfigManager::instance().machine("haiTian"));

        // --- 契约: HTTP 入口的 reqSource 固定 http, 报文带 reqSource 带不偏 ---
        controller.handleHttp("/93/11/washcar/start", "{\"reqSource\":\"mqtt\"}");
        expect(lobby.lastReqSource == "http",
               "2.9 契约: HTTP 入口把 reqSource 强制成 http(报文里带 mqtt 也不认, 否则能绕开维修模式拦截)");

        // --- 现状: 海天机型的回执 plcId 取的是 plcTcp 段(驿公里的配置) ---
        r = controller.handleHttp("/93/11/washcar/stop", "{}");
        expect(r.body.find("\"plcId\":\"tcp_plc_01\"") != std::string::npos,
               "2.9 基线: 海天 stop 回执 plcId=tcp_plc_01(目标: fx_plc_01, 口径变更时改这里)");
    }

    // ===== 2.10 互斥控制红线: 维修模式只挡云端, 单点控制只在维修模式放行 =====
    // AGENTS.md"不可破的业务规则"第 1 条。重构时最容易改坏的就是这两条,
    // 写死在测试里当红线: 改坏了这里立刻红, 不用等到现场才发现。
    {
        resetMaintenanceFlag();
        FakeDeviceService device;
        Timer timer;
        HaiTianWashCarService service(device, timer);
        const WashCarOperation op = makeOperation();

        // 非维修模式: 单点控制被拒
        WashCarOperationResult point = service.pointOn(makePointOperation("M7"));
        expect(point.getCode() == ErrorCode::Code::WASHCAR_STATE_MACHINE_REJECTED,
               "2.10 互斥: 非维修模式单点控制被拒");
        expect(point.getMessage().find("单点控制只允许在维修模式") != std::string::npos,
               "2.10 互斥: 拒绝理由指向维修模式");

        expect(service.maintain(op).getCode() == ErrorCode::Code::SUCCESS, "2.10 互斥: 进维修模式");

        // 维修模式: 云端启动/复位被拒
        WashCarOperationResult cloudStart = service.startWashCar(makeOperation("mqtt"));
        expect(cloudStart.getCode() == ErrorCode::Code::WASHCAR_MAINTAINING,
               "2.10 互斥: 维修模式下云端启动被拒");
        expect(cloudStart.getMessage() == "maintaining", "2.10 互斥: 云端被拒回执带 maintaining");

        WashCarOperationResult cloudReset = service.resetWashCar(makeOperation("mqtt"));
        expect(cloudReset.getCode() == ErrorCode::Code::WASHCAR_MAINTAINING,
               "2.10 互斥: 维修模式下云端复位被拒");

        // 维修模式: 单点控制放行, 而且真的写了点位
        const int writesBefore = device.writes;
        WashCarOperationResult pointOn = service.pointOn(makePointOperation("M7"));
        expect(pointOn.getCode() == ErrorCode::Code::SUCCESS, "2.10 互斥: 维修模式下单点控制放行");
        expect(device.writes == writesBefore + 1, "2.10 互斥: 单点控制真的写了点位");
        expect(wroteM(device, 7, true), "2.10 互斥: 写的是 M7=1");

        // 退出维修模式
        expect(service.maintain(op).getMessage() == "maintenance off", "2.10 互斥: 退出维修模式");
        resetMaintenanceFlag();
    }

    std::cout << "washcar_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
