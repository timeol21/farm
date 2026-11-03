// 洗车机状态机自检。
// 每台洗车机一个子类:海天一套规则、驿公里一套规则,基类管通用命令策略。
// 用法: ./washcar_state_machine_test
#include "business_layer/washcar/washcar_state_machine.h"
#include "common/config/config_manager.h"
#include "common/config/default_point.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace washcar::cfg;

static int g_pass = 0, g_fail = 0;

static void CHECK(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::cout << "  FAIL: " << what << std::endl;
    }
}

static const SignalConfig* findSignal(const MachinePointTable& table, const std::string& name) {
    for (const auto& signal : table.signals) {
        if (signal.name == name) return &signal;
    }
    return nullptr;
}

// 海天:一台正常待机的洗车机。启动准入 4 条规则涉及的点都放成"通过"。
static PlcPointValues haiTianBaseline() {
    return PlcPointValues{{{"Y74", 0}, {"D142", 1}, {"Y50", 0}, {"Y51", 0}, {"Y52", 1},
                           {"M187", 1}, {"M1", 1}, {"X44", 0}, {"S0", 0},
                           {"D28", 0}, {"X50", 1}, {"M257", 0}, {"M259", 0}, {"M381", 0},
                           {"X62", 1}, {"X63", 1}, {"X46", 1}, {"X3", 0}}};
}

static PlcPointValues withValue(const char* point, int value) {
    PlcPointValues points = haiTianBaseline();
    points.values[point] = value;
    return points;
}

// 驿公里:待机 S5 + 车位到位
static PlcPointValues yiGongLiBaseline() {
    return PlcPointValues{{{"S5", 1}, {"M510", 0}, {"M282", 1}, {"D1000", 0},
                           {"M147", 0}, {"M148", 0}, {"M169", 0}}};
}

static PlcPointValues yiGongLiWithValue(const char* point, int value) {
    PlcPointValues points = yiGongLiBaseline();
    points.values[point] = value;
    return points;
}

int main() {
    ConfigManager::instance().applyDefaults();
    {
        std::string error;
        const auto& pointTable = ConfigManager::instance().machine();
        CHECK(pointTable.machineType == "haiTian", "默认点位表是海天");
        CHECK(validatePointTable(pointTable, error), "海天点位表自检通过: " + error);
    }

    HaiTianStateMachine haiTianMachine;
    YiGongLiStateMachine yiGongLiMachine;

    // 状态派生:空闲 / 运作 / 故障
    {
        auto status = haiTianMachine.buildStatus(haiTianBaseline());
        CHECK(status.state == ServiceState::IDLE, "无异常未洗车 → 空闲");
        CHECK(!status.washing && !status.hasFault && !status.needsReset, "基准状态三个标志都 false");
        CHECK(status.allowStart, "基准状态允许启动");

        status = haiTianMachine.buildStatus(withValue("Y74", 1));
        CHECK(status.state == ServiceState::WORKING, "Y74=1 → 运作");
        CHECK(status.washing, "Y74=1 → washing 标志 true");

        status = haiTianMachine.buildStatus(withValue("D28", 68));
        CHECK(status.state == ServiceState::FAULT, "D28=68 → 故障");
        CHECK(status.hasFault && status.faultCode == 68, "D28=68 → hasFault + 故障码原值");

        auto both = withValue("D28", 60);
        both.values["Y74"] = 1;
        CHECK(haiTianMachine.buildStatus(both).state == ServiceState::FAULT, "故障 + 洗车中 → 故障优先");

        // 需要先复位(X44 / S0):不改状态,只影响复位命令
        status = haiTianMachine.buildStatus(withValue("X44", 1));
        CHECK(status.needsReset && status.state == ServiceState::IDLE && !status.hasFault,
              "X44=1 → needsReset,不改状态,不算故障");
        CHECK(haiTianMachine.buildStatus(withValue("S0", 1)).needsReset, "S0=1 → needsReset");
    }

    // 海天启动准入 4 条规则(照搬原服务层的前置检查)
    {
        auto status = haiTianMachine.buildStatus(withValue("M187", 0));
        CHECK(!status.allowStart, "M187=0 → 不允许启动");
        CHECK(status.blockReason.find("washPosition incorrect") != std::string::npos,
              "M187=0 理由是 washPosition incorrect");

        auto advanced = withValue("M187", 0);
        advanced.values["X3"] = 1;
        CHECK(haiTianMachine.buildStatus(advanced).blockReason.find("parking advanced") != std::string::npos,
              "M187=0 且 X3=1 理由是 parking advanced");

        status = haiTianMachine.buildStatus(withValue("M1", 0));
        CHECK(!status.allowStart && status.blockReason.find("startCondition not") != std::string::npos,
              "M1=0 → 不允许启动,理由 startCondition not");

        status = haiTianMachine.buildStatus(withValue("X62", 0));
        CHECK(!status.allowStart && status.blockReason.find("anti-collision") != std::string::npos,
              "X62=0 → 不允许启动,理由撞了防撞杆");
        CHECK(!haiTianMachine.buildStatus(withValue("X63", 0)).allowStart, "X63=0 → 不允许启动");

        status = haiTianMachine.buildStatus(withValue("X46", 0));
        CHECK(!status.allowStart && status.blockReason.find("vehicle too tall") != std::string::npos,
              "X46=0 → 不允许启动,理由车辆过高");

        // 读不到点位 = 放行(原实现是"读到且为 0 才拒绝")
        auto missing = haiTianBaseline();
        missing.values.erase("M187");
        missing.values.erase("M1");
        missing.values.erase("X46");
        CHECK(haiTianMachine.buildStatus(missing).allowStart, "海天:读不到前置点位 → 放行");
    }

    // 驿公里:状态派生 + 启动准入 3 条规则
    {
        auto status = yiGongLiMachine.buildStatus(yiGongLiBaseline());
        CHECK(status.state == ServiceState::IDLE && status.allowStart, "驿公里待机 → 空闲,允许启动");

        auto washing = yiGongLiBaseline();
        washing.values.erase("S5");
        washing.values["S20"] = 1;
        status = yiGongLiMachine.buildStatus(washing);
        CHECK(status.state == ServiceState::WORKING && status.washing, "驿公里 S20 → 运作");

        status = yiGongLiMachine.buildStatus(yiGongLiWithValue("D1000", 105));
        CHECK(status.state == ServiceState::FAULT && status.faultCode == 105, "驿公里 D1000=105 → 故障");

        auto notStandby = yiGongLiBaseline();
        notStandby.values.erase("S5");
        notStandby.values["S30"] = 1;
        status = yiGongLiMachine.buildStatus(notStandby);
        CHECK(!status.allowStart && status.blockReason.find("not in standby") != std::string::npos,
              "驿公里非待机步 → 不允许启动");

        status = yiGongLiMachine.buildStatus(yiGongLiWithValue("M510", 1));
        CHECK(!status.allowStart && status.blockReason.find("paused service") != std::string::npos,
              "驿公里 M510=1 → 不允许启动");

        status = yiGongLiMachine.buildStatus(yiGongLiWithValue("M282", 0));
        CHECK(!status.allowStart && status.blockReason.find("car not positioned") != std::string::npos,
              "驿公里 M282=0 → 不允许启动");

        auto missingCar = yiGongLiBaseline();
        missingCar.values.erase("M282");
        CHECK(!yiGongLiMachine.buildStatus(missingCar).allowStart, "驿公里:读不到 M282 → 拒绝(跟原实现一致)");
    }

    // 通用命令策略(基类)
    {
        const auto idle = haiTianMachine.buildStatus(haiTianBaseline());
        const auto washing = haiTianMachine.buildStatus(withValue("Y74", 1));
        const auto faulty = haiTianMachine.buildStatus(withValue("D28", 60));

        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), idle, false, WashCarCommand::STOP).allowed,
              "停止永远允许(空闲)");
        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), washing, false, WashCarCommand::STOP).allowed,
              "停止永远允许(洗车中)");
        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), faulty, false, WashCarCommand::STOP).allowed,
              "停止永远允许(故障时)");

        const auto blocked = haiTianMachine.checkCommand(withValue("M1", 0),
                                haiTianMachine.buildStatus(withValue("M1", 0)), false, WashCarCommand::START);
        CHECK(!blocked.allowed && blocked.reason.find("startCondition not") != std::string::npos,
              "启动:条件由子类判,拒绝带具体原因");

        CHECK(haiTianMachine.checkCommand(withValue("X44", 1), haiTianMachine.buildStatus(withValue("X44", 1)),
                                          false, WashCarCommand::RESET).allowed, "复位不被挡");

        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), idle, false, WashCarCommand::ENTER_MAINTENANCE).allowed,
              "空闲可进维修");
        CHECK(!haiTianMachine.checkCommand(haiTianBaseline(), washing, false, WashCarCommand::ENTER_MAINTENANCE).allowed,
              "洗车中不可进维修");
        CHECK(!haiTianMachine.checkCommand(haiTianBaseline(), idle, true, WashCarCommand::ENTER_MAINTENANCE).allowed,
              "已在维修不可重复进入");
        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), idle, true, WashCarCommand::EXIT_MAINTENANCE).allowed,
              "维修中可退出");
        CHECK(!haiTianMachine.checkCommand(haiTianBaseline(), idle, false, WashCarCommand::EXIT_MAINTENANCE).allowed,
              "非维修不可退出");

        CHECK(haiTianMachine.checkCommand(haiTianBaseline(), idle, true, WashCarCommand::POINT_ON).allowed,
              "维修中可点动");
        CHECK(!haiTianMachine.checkCommand(haiTianBaseline(), idle, false, WashCarCommand::POINT_ON).allowed,
              "非维修不可点动");
        CHECK(!haiTianMachine.checkCommand(haiTianBaseline(), idle, false, WashCarCommand::POINT_OFF).allowed,
              "非维修不可取消点动");
    }

    // checkAndApply: log_only 只记日志, enforce 才真拒
    {
        const auto blocked = withValue("M1", 0);
        ConfigManager::instance().setStateMachineModeForTest("log_only");
        CHECK(!haiTianMachine.checkAndApply(blocked, false, WashCarCommand::START, "startWashCar"),
              "log_only:本应拒绝的命令照常执行");
        ConfigManager::instance().setStateMachineModeForTest("enforce");
        CHECK(haiTianMachine.checkAndApply(blocked, false, WashCarCommand::START, "startWashCar"), "enforce:真拒");
        CHECK(!haiTianMachine.checkAndApply(haiTianBaseline(), false, WashCarCommand::START, "startWashCar"),
              "enforce:条件满足照样放行");
        ConfigManager::instance().setStateMachineModeForTest("log_only");
    }

    // 复位自检 + 描述 + 缺点位
    {
        const auto& pointTable = default_point::getPointTable("haiTian");
        CHECK(pointTable.homeSensors.size() == 11, "海天归位传感器 11 个");

        auto allHome = haiTianBaseline();
        for (const auto& name : pointTable.homeSensors) {
            const auto* signal = findSignal(pointTable, name);
            if (signal) allHome.values[signal->point] = 1;
        }
        haiTianMachine.noteResetConfirmed(allHome, false);
        auto stillFault = allHome;
        stillFault.values["D28"] = 60;
        haiTianMachine.noteResetConfirmed(stillFault, false);
        auto oneOff = allHome;
        oneOff.values["X4"] = 0;
        haiTianMachine.noteResetConfirmed(oneOff, false);
        CHECK(haiTianMachine.buildStatus(haiTianBaseline()).state == ServiceState::IDLE, "自检不改变状态");

        const auto text = haiTianMachine.describeNow(withValue("D28", 68), false);
        CHECK(text.find("状态=故障") != std::string::npos, "描述里有状态");
        CHECK(text.find("车辆高度不符合要求") != std::string::npos, "描述里有故障码中文说明");

        const PlcPointValues empty;
        CHECK(haiTianMachine.buildStatus(empty).state == ServiceState::IDLE, "没点位时是默认值");
        CHECK(haiTianMachine.describeNow(empty, false).find("未就绪") != std::string::npos,
              "没点位时描述返回未就绪");
        CHECK(!haiTianMachine.checkAndApply(empty, false, WashCarCommand::START, "startWashCar"),
              "没点位时不判定(放行)");
    }

    std::cout << "washcar_state_machine_test: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
