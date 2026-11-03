#include "common/config/default_point.h"

namespace {

// ============ 海天(FX 串口) ============
const washcar::cfg::MachinePointTable& haiTianPointTable() {
    using namespace washcar::cfg;
    static const MachinePointTable table = []{
        MachinePointTable table;
        table.machineType = "haiTian";
        table.protocol = "fxSerial";
        table.description = "海天洗车机。FX 串口协议,位点轮询。";

        table.acquisition.dPoints = {"D28", "D142"};
        table.acquisition.yPoints = {"Y50", "Y51", "Y52", "Y55", "Y74"};
        table.acquisition.mPoints = {
            "M0", "M1", "M47", "M100", "M187",
            "M220", "M221", "M223", "M224", "M225",
            "M250", "M251", "M257", "M258", "M259",
            "M381", "M400", "M500", "M616"
        };
        table.acquisition.xPoints = {
            "X3", "X20",
            "X32", "X33", "X34", "X35", "X36", "X37",
            "X40", "X41", "X42", "X43", "X44", "X45", "X46", "X47",
            "X50", "X62", "X63", "X74"
        };
        table.acquisition.sPoints = {"S0"};

        table.signals.push_back({"stateRegister",  "D142", SignalReport::REPORT_PROPERTY, "", "洗车步骤 0-10"});
        table.signals.push_back({"washing",        "Y74",  SignalReport::REPORT_PROPERTY, "", "洗车标志"});
        table.signals.push_back({"forward",        "Y50",  SignalReport::REPORT_PROPERTY, "", "前进提示"});
        table.signals.push_back({"back",           "Y51",  SignalReport::REPORT_PROPERTY, "", "后退提示"});
        table.signals.push_back({"stopcar",        "Y52",  SignalReport::REPORT_PROPERTY, "", "停车提示"});
        table.signals.push_back({"startCondition", "M1",   SignalReport::REPORT_PROPERTY, "", "启动前置条件位"});
        table.signals.push_back({"washPosition",   "M187", SignalReport::REPORT_PROPERTY, "", "车辆已停放到位"});

        // cloudKey 只有故障码跟点位名不同:云端 Excel 里属性名写的是 fault,值是 D28 里的码。
        table.signals.push_back({"fault",         "D28",  SignalReport::REPORT_EVENT, "fault", "故障码"});
        table.signals.push_back({"foamShort",     "M257", SignalReport::REPORT_EVENT, "", "发泡剂不足"});
        table.signals.push_back({"waxShort",      "M259", SignalReport::REPORT_EVENT, "", "蜡水不足"});
        table.signals.push_back({"stopPressed",   "X44",  SignalReport::REPORT_EVENT, "", "按了停止"});
        // X50 是低电平有效:0 = 水位不足,1 = 正常。跟其它故障位方向相反,normalValue = 1。
        table.signals.push_back({"waterLow",      "X50",  SignalReport::REPORT_EVENT, "", "水位不足(0=不足)", 1});
        table.signals.push_back({"walkPulse",     "M381", SignalReport::REPORT_EVENT, "", "行走脉冲异常"});
        table.signals.push_back({"deviceStopped", "S0",   SignalReport::REPORT_EVENT, "", "设备在停止状态"});

        // X62 / X63 是左右防撞杆(不是归位传感器);X46 是车辆高度检测。
        // 归位传感器部件待现场确认,先按编号命名。
        table.signals.push_back({"antiCollisionBarLeft",  "X62", SignalReport::REPORT_NONE, "", "左防撞杆(1=未触发)"});
        table.signals.push_back({"antiCollisionBarRight", "X63", SignalReport::REPORT_NONE, "", "右防撞杆(1=未触发)"});
        table.signals.push_back({"vehicleHeightOk", "X46", SignalReport::REPORT_NONE, "", "车辆高度检测(1=高度OK)"});
        // X3 = 车辆已进位(停车超位)。只用于启动被拒时解释原因, 不上报。
        table.signals.push_back({"parkingAdvanced", "X3",  SignalReport::REPORT_NONE, "", "车辆已进位(停车超位)"});
        table.signals.push_back({"keyState", "M616", SignalReport::REPORT_NONE, "", "PLC 关键状态位(业务含义待确认)"});
        table.keyStatePoints = {"stopcar", "startCondition", "washPosition", "stateRegister", "keyState"};

        // 写点位跟服务层实际下发的 PLC 写操作一致(见 HaiTianWashcar_service.cpp 的 forceFx 调用)。
        table.services.push_back({"start",     "Manual start", {{"default", "X45"}},  "启动洗车"});
        table.services.push_back({"stop",      "stop",         {{"default", "M0"}},   "停止洗车(M0=1)"});
        table.services.push_back({"clearStop", "",             {{"default", "M0"}},   "清停止标志(复位前先写 0)"});
        table.services.push_back({"reset",     "reset",        {{"default", "M100"}}, "复位(先清停止标志,再 M100=1)"});

        table.dailyReportSignals = {"foamShort", "waxShort"};

        table.faultSignals = {"fault"};
        table.resetSignals = {"stopPressed", "deviceStopped"};

        // 启动准入 4 条(原写在 HaiTianStateMachine::checkStart 里, 现为数据)。

        // missing=allow: PLC 断连读不到点时放行, 不在断连时把顾客的启动全拒了。
        table.startChecks = {
            {"signal", "washPosition", 1, "allow", "washPosition incorrect",
             "请确认车辆已停放到指定位置",
             {{"parkingAdvanced", 1, "parking advanced"}}},
            {"signal", "startCondition", 1, "allow", "startCondition not",
             "启动前置条件不满足,请确认车辆和设备状态", {}},
            {"signal", "antiCollisionBarLeft", 1, "allow", "vehicle hit anti-collision bar",
             "车辆压到防撞杆,请把车挪开后重试", {}},
            {"signal", "antiCollisionBarRight", 1, "allow", "vehicle hit anti-collision bar",
             "车辆压到防撞杆,请把车挪开后重试", {}},
            {"signal", "vehicleHeightOk", 1, "allow", "vehicle too tall",
             "车辆高度超出洗车机允许范围", {}},
        };
        for (auto& rule : table.startChecks) rule.missingSpecified = true;

        // 故障码说明:只用于日志和状态查询,不影响准入判断。
        table.faultCodes = {
            {"68", "车辆高度不符合要求"}, {"67", "车辆宽度不符合要求"},
            {"63", "车辆宽度不符合要求"}, {"54", "车辆宽度不符合要求"},
            {"51", "车辆宽度不符合要求"}, {"62", "未检测到车头"},
            {"71", "当前环境不适合洗车"}, {"72", "水箱液位不足"},
            {"66", "水流量不足"},           {"53", "吹风检测后异常"},
            {"52", "吹风检测前异常"},       {"31", "吹风上下限位异常"},
            {"30", "顶刷上下限位异常"},     {"29", "前后限位异常"},
            {"27", "轮胎开位检测异常"},     {"32", "侧刷开位脱离后状态突变"},
            {"28", "侧刷开合位状态突变"},   {"65", "顶刷右吊带保护触发"},
            {"64", "顶刷左吊带保护触发"},   {"60", "顶刷升降或运动反馈异常"},
            {"36", "洗车身时右侧刷大角度偏离"}, {"35", "洗车身时左侧刷大角度偏离"},
            {"34", "车辆超长或行走脉冲异常"}, {"55", "多级泵负载异常"},
            {"50", "大风机侧负载异常"},     {"49", "大风机中负载异常"},
            {"33", "顶刷触杠检测异常"}
        };

        // 归位自检: 7-13 现场验证过的那版没有这道门(复位后不查归位), 这里保持空清单。
        // 字段本身留着 —— 它是 config.json 的 schema 一部分, 将来要恢复自检时填名字即可。
        return table;
    }();
    return table;
}

// ============ 驿公里(Modbus TCP) ============
const washcar::cfg::MachinePointTable& yiGongLiPointTable() {
    using namespace washcar::cfg;
    static const MachinePointTable table = []{
        MachinePointTable table;
        table.machineType = "yiGongLi";
        table.protocol = "modbusTcp";
        table.description = "驿公里洗车机。Modbus TCP word 协议,故障走 D1000 码,流程走 S 步号。";

        table.acquisition.dPoints = {"D1000", "D1028", "D1030", "D1032",
                                    "D1210", "D1211", "D1212",
                                    "D2000", "D2001", "D2002", "D2005", "D2006"};
        table.acquisition.mPoints = {
            "M147", "M148", "M169", "M270", "M282",
            "M301", "M302", "M303", "M304", "M305",
            "M306", "M307", "M308", "M309", "M310",
            "M311", "M312", "M501", "M510"
        };
        for (int step = 0; step <= 81; ++step) {
            table.acquisition.sPoints.push_back("S" + std::to_string(step));
        }

        table.signals.push_back({"deviceStatus",  "D1210", SignalReport::REPORT_PROPERTY, "", "设备状态(ASCII)"});
        table.signals.push_back({"washDuration", "D2000", SignalReport::REPORT_PROPERTY, "", "洗车时长(秒)"});
        table.signals.push_back({"valueD2001", "D2001", SignalReport::REPORT_PROPERTY, "", "阶段相关寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD2002", "D2002", SignalReport::REPORT_PROPERTY, "", "阶段相关寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD2005", "D2005", SignalReport::REPORT_PROPERTY, "", "阶段相关寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD2006", "D2006", SignalReport::REPORT_PROPERTY, "", "阶段相关寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD1028", "D1028", SignalReport::REPORT_PROPERTY, "", "刷子电流类监控寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD1030", "D1030", SignalReport::REPORT_PROPERTY, "", "刷子电流类监控寄存器(含义待现场确认)"});
        table.signals.push_back({"valueD1032", "D1032", SignalReport::REPORT_PROPERTY, "", "刷子电流类监控寄存器(含义待现场确认)"});
        table.signals.push_back({"fault",        "D1000", SignalReport::REPORT_EVENT, "d1000", "故障码"});
        table.signals.push_back({"carArrived",   "M282", SignalReport::REPORT_NONE, "", "车辆已到位"});
        table.signals.push_back({"pausedService", "M510", SignalReport::REPORT_NONE, "", "暂停服务"});
        table.signals.push_back({"unsafeState",  "M147", SignalReport::REPORT_EVENT, "", "不安全状态"});
        table.signals.push_back({"hardwareFault","M148", SignalReport::REPORT_EVENT, "", "硬件故障"});
        table.signals.push_back({"collision",    "M169", SignalReport::REPORT_EVENT, "", "防撞触发"});

        table.keyStatePoints = {"deviceStatus", "washDuration"};

        // 驿公里独有命令:启动按模式选点位,复位分普通/安全两种
        table.services.push_back({"startPrepare", "",             {{"default", "M204"}}, "启动准备(先置 M204=1,等 200ms 再下发模式启动)"});
        table.services.push_back({"start",        "Manual start", {{"1", "M521"}, {"2", "M522"}, {"3", "M523"}}, "启动洗车(按模式,前置 M204=1)"});
        table.services.push_back({"stop",         "stop",         {{"default", "M205"}}, "停止洗车(M205=1)"});
        table.services.push_back({"abort",        "",             {{"default", "M206"}}, "中止洗车(M206=1)"});
        table.services.push_back({"reset",        "reset",        {{"default", "M202"}}, "复位(M202=1)"});
        table.services.push_back({"resetSafe",    "",             {{"default", "M203"}}, "安全复位(M203=1)"});
        table.services.push_back({"stopWork",     "",             {{"default", "M501"}}, "停止营业(M501=1)"});
        table.services.push_back({"stopService",  "",             {{"default", "M510"}}, "停止营业2(暂停服务,M510=1)"});

        // 启动准入 3 条(原写在 YiGongLiStateMachine::checkStart 里, 现为数据)。
        // 当前步必须 S5: 没有 S 数据时跳过(allow); M282 读不到要拒(reject)。
        table.startChecks = {
            {"activeStep", "", 5, "allow", "not in standby (S{step})",
             "设备不在待机状态,洗车中/归位中/停止中都不能启动", {}},
            {"signal", "pausedService", 0, "allow", "paused service (M510=1)",
             "服务已暂停,先恢复服务再启动", {}},
            {"signal", "carArrived", 1, "reject", "car not positioned (M282=0)",
             "请把车停到指定位置", {}},
        };
        for (auto& rule : table.startChecks) rule.missingSpecified = true;

        // 故障位
        table.faultSignals = {"fault"};

        // 故障码表:D1000 里的码 -> 云端 faultName 用的标识(进故障上报报文)。
        table.faultCodes = {
            {"101", "left_brush_not_closed"},  {"102", "right_brush_not_closed"},
            {"103", "top_brush_current_high"}, {"105", "estop"},
            {"106", "abort"},                   {"107", "vertical_limit_conflict"},
            {"108", "fwd_bwd_limit_conflict"},  {"109", "water_shortage"},
            {"110", "top_brush_overcurrent"},   {"111", "left_brush_overcurrent"},
            {"112", "right_brush_overcurrent"}, {"113", "belt_loose"},
            {"114", "top_brush_comm_error"},    {"115", "gantry_comm_error"},
            {"116", "comm_error_limit"},        {"117", "comm_error_limit2"},
            {"118", "comm_error_limit3"},       {"119", "left_anti_collision"},
            {"120", "right_anti_collision"},    {"121", "pedestrian_anti_collision"}
        };
        return table;
    }();
    return table;
}

}  // namespace

namespace default_point {

const washcar::cfg::MachinePointTable& getPointTable(const std::string& boxType) {
    return boxType == "yiGongLi" ? yiGongLiPointTable() : haiTianPointTable();
}

}  // namespace default_point
