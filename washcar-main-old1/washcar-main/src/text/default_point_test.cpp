// 洗车机点位表自检:两份内置表自洽,校验规则能挡住坏表。
// 用法: ./default_point_test
#include "common/config/default_point.h"
#include "common/config/config_manager.h"

#include <algorithm>
#include <iostream>
#include <string>
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

static const ServiceConfig* findService(const MachinePointTable& table, const std::string& name) {
    for (const auto& service : table.services) {
        if (service.name == name) return &service;
    }
    return nullptr;
}

static std::string servicePointOf(const MachinePointTable& table, const std::string& serviceName,
                                  const std::string& selector = "default") {
    const ServiceConfig* service = findService(table, serviceName);
    if (!service) return {};
    auto entry = service->write.find(selector);
    if (entry == service->write.end() && selector != "default") {
        entry = service->write.find("default");
    }
    return entry == service->write.end() ? std::string{} : entry->second;
}

static std::string cloudKeyOf(const SignalConfig& signal) {
    if (!signal.cloudKey.empty()) return signal.cloudKey;
    std::string key = signal.point;
    for (auto& character : key) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return key;
}

int main() {
    for (const char* boxType : {"haiTian", "yiGongLi"}) {
        std::string error;
        const auto& pointTable = default_point::getPointTable(boxType);
        CHECK(validatePointTable(pointTable, error),
              std::string(boxType) + " 内置表校验通过: " + error);
        CHECK(pointTable.machineType == boxType, std::string(boxType) + " machineType 对得上");
    }
    CHECK(default_point::getPointTable("somethingNew").machineType == "haiTian", "未知机型回退海天");

    const auto& haiTian = default_point::getPointTable("haiTian");
    const auto& yiGongLi = default_point::getPointTable("yiGongLi");

    // 事件点位集合 + 上报字段名必须跟改造前一致(D28 已改名 fault)
    {
        std::vector<std::string> eventPoints, eventFields, propertyFields;
        for (const auto& signal : haiTian.signals) {
            if (signal.report == SignalReport::REPORT_EVENT) {
                eventPoints.push_back(signal.point);
                eventFields.push_back(cloudKeyOf(signal));
            } else if (signal.report == SignalReport::REPORT_PROPERTY) {
                propertyFields.push_back(cloudKeyOf(signal));
            }
        }
        std::sort(eventPoints.begin(), eventPoints.end());
        std::sort(eventFields.begin(), eventFields.end());
        std::sort(propertyFields.begin(), propertyFields.end());
        CHECK(eventPoints == std::vector<std::string>({"D28", "M257", "M259", "M381", "S0", "X44", "X50"}),
              "海天事件点位集合跟改造前一致");
        CHECK(eventFields == std::vector<std::string>({"M257", "M259", "M381", "S0", "X44", "X50", "fault"}),
              "海天故障上报字段名跟改造前一致");
        CHECK(propertyFields == std::vector<std::string>({"D142", "M1", "M187", "Y50", "Y51", "Y52", "Y74"}),
              "海天变化上报字段名跟改造前一致");
    }

    // 故障码:海天 D28 / 驿公里 D1000
    CHECK(findSignal(haiTian, "fault")->point == "D28", "海天 fault 绑 D28");
    CHECK(cloudKeyOf(*findSignal(haiTian, "fault")) == "fault", "海天故障码字段名是 fault");
    CHECK(findSignal(haiTian, "washing")->point == "Y74", "海天 washing 绑 Y74");
    CHECK(cloudKeyOf(*findSignal(yiGongLi, "fault")) == "d1000", "驿公里故障码字段名是 d1000");
    CHECK(!yiGongLi.faultCodes.empty(), "驿公里表带故障码名称表");

    // 服务写点位
    CHECK(servicePointOf(haiTian, "start") == "X45", "海天 start 写 X45");
    CHECK(servicePointOf(haiTian, "stop") == "M0", "海天 stop 写 M0");
    CHECK(servicePointOf(haiTian, "reset") == "M100", "海天 reset 写 M100");
    CHECK(servicePointOf(haiTian, "noSuchService").empty(), "不存在的服务返回空串");
    CHECK(servicePointOf(yiGongLi, "start", "1") == "M521", "驿公里 start 模式1 = M521");
    CHECK(servicePointOf(yiGongLi, "start", "3") == "M523", "驿公里 start 模式3 = M523");
    CHECK(servicePointOf(yiGongLi, "start").empty(), "驿公里 start 没有 default 条件");
    CHECK(servicePointOf(yiGongLi, "startPrepare") == "M204", "驿公里 startPrepare 写 M204");
    CHECK(servicePointOf(yiGongLi, "reset") == "M202", "驿公里 reset 写 M202");
    CHECK(servicePointOf(yiGongLi, "resetSafe") == "M203", "驿公里 resetSafe 写 M203");
    CHECK(servicePointOf(yiGongLi, "abort") == "M206", "驿公里 abort 写 M206");

    // 数量约定
    {
        int propertyCount = 0, eventCount = 0;
        for (const auto& signal : haiTian.signals) {
            if (signal.report == SignalReport::REPORT_PROPERTY) ++propertyCount;
            else if (signal.report == SignalReport::REPORT_EVENT) ++eventCount;
        }
        CHECK(propertyCount == 7, "海天属性数 = 7 (实际 " + std::to_string(propertyCount) + ")");
        CHECK(eventCount == 7, "海天事件数 = 7 (实际 " + std::to_string(eventCount) + ")");
        int yiPropertyCount = 0;
        for (const auto& signal : yiGongLi.signals) {
            if (signal.report == SignalReport::REPORT_PROPERTY) ++yiPropertyCount;
        }
        CHECK(yiPropertyCount == 9, "驿公里属性数 = 9 (实际 " + std::to_string(yiPropertyCount) + ")");
        CHECK(yiGongLi.acquisition.sPoints.size() == 82, "驿公里 S 点 82 个");
    }

    // 归位传感器 11 个(X62 / X63 是防撞杆,不算归位传感器)
    CHECK(haiTian.homeSensors.size() == 11,
          "海天归位传感器 11 个 (实际 " + std::to_string(haiTian.homeSensors.size()) + ")");
    CHECK(findSignal(haiTian, "antiCollisionBarLeft")->point == "X62", "左防撞杆绑 X62");
    CHECK(findSignal(haiTian, "antiCollisionBarRight")->point == "X63", "右防撞杆绑 X63");
    CHECK(findSignal(haiTian, "vehicleHeightOk")->point == "X46", "车辆高度检测绑 X46");

    // 校验规则要能挡住坏表
    {
        std::string error;
        auto broken = default_point::getPointTable("haiTian");
        broken.signals.push_back(broken.signals[0]);
        CHECK(!validatePointTable(broken, error), "重名信号被挡下");
        CHECK(error.find("重复") != std::string::npos, "重名提示里有'重复'");

        auto brokenRef = default_point::getPointTable("haiTian");
        brokenRef.faultSignals.push_back("noSuchSignal");
        CHECK(!validatePointTable(brokenRef, error), "引用不存在的信号名被挡下");

        auto brokenFormat = default_point::getPointTable("haiTian");
        brokenFormat.acquisition.mPoints.push_back("Mxx");
        CHECK(!validatePointTable(brokenFormat, error), "非法点位格式被挡下");

        auto brokenOrphan = default_point::getPointTable("haiTian");
        brokenOrphan.signals.push_back({"orphan", "M9999", SignalReport::REPORT_NONE, "", "没采集的点位"});
        CHECK(!validatePointTable(brokenOrphan, error), "点位不在采集清单被挡下");
        CHECK(error.find("M9999") != std::string::npos, "提示里带出孤儿点位");

    }

    std::cout << "default_point_test: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
