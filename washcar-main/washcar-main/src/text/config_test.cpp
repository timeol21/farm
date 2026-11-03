// config_test.cpp — 配置服务自检。
// 覆盖四块:
//   1. 配置读写与校验(缺字段报错、模板生成、坏 JSON 不覆盖原文件、timing clamp、startChecks 覆盖)
//   2. 内置点位表自洽 + 校验规则能挡住坏表
//   3. 配置补丁: 校验 / 保存 / 回滚
//   4. 配置台健壮性: 畸形请求不能把盒子进程带崩
//   5. 日志查询(配置台日志页的数据源)
// 用法: ./config_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "business_layer/command/controller.h"
#include "business_layer/safety/auth_service.h"
#include "common/config/config_manager.h"
#include "common/config/config_object.h"
#include "common/config/config_serialization.h"
#include "common/config/default_config.cpp"   // 需要 default_config::toJson() 比对模板内容
#include "common/config/default_point.h"
#include "common/config/point.h"
#include "common/log/log_manager.h"
#include "common/log/log_query.h"
#include "presentation_layer/admin_controller.h"
#include "presentation_layer/http_request.h"
#include "presentation_layer/http_response.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
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

const SignalConfig* findSignal(const MachinePointTable& table, const std::string& name) {
    for (const auto& signal : table.signals) {
        if (signal.name == name) return &signal;
    }
    return nullptr;
}

const ServiceConfig* findService(const MachinePointTable& table, const std::string& name) {
    for (const auto& service : table.services) {
        if (service.name == name) return &service;
    }
    return nullptr;
}

std::string servicePointOf(const MachinePointTable& table, const std::string& serviceName,
                           const std::string& selector = "default") {
    const ServiceConfig* service = findService(table, serviceName);
    if (!service) return {};
    auto entry = service->write.find(selector);
    if (entry == service->write.end() && selector != "default") {
        entry = service->write.find("default");
    }
    return entry == service->write.end() ? std::string{} : entry->second;
}

std::string cloudKeyOf(const SignalConfig& signal) {
    if (!signal.cloudKey.empty()) return signal.cloudKey;
    std::string key = signal.point;
    for (auto& character : key) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return key;
}

void writeTempJson(const std::string& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------- 配置台那两个替身 ----------

template <typename T>
LobbyResult<T> stubError() {
    return LobbyResult<T>::Error(ErrorCode::Code::SUCCESS);
}

// 大厅接口替身:配置台的鉴权/参数校验路径不碰洗车业务。
class StubLobbyService : public ILobbyService {
public:
    bool washing = false;

    std::vector<FxPlcStatus> viewPlcStatus() override { return {}; }
    LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery&) override
        { return stubError<FrameQuery>(); }
    LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery&) override
        { return stubError<FrameQuery>(); }
    LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery&) override
        { return stubError<SensorQuery>(); }
    bool isWashCarWashing() override { return washing; }
    bool isMqttConnected() override { return true; }
    LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo&) override
        { return stubError<SensorQuery>(); }
    LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation&) override
        { return stubError<SolenoidValveOperationResult>(); }
    LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> abortWashCar(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> stopWork(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> stopService(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> resetSafe(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<bool> changeMode(const WashCarOperation&) override { return stubError<bool>(); }
    LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation&) override
        { return stubError<WashCarOperationResult>(); }
    std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string&) override { return nullptr; }
    bool checkDetectionBeforeWashCar(const std::string&) override { return true; }
    void onPointValueChanged(const std::string&, const std::string&, bool) override {}
};

// 设备接口替身:只有"测试读"和状态查询两条会被配置台用到。
class StubDeviceService : public IDeviceService {
public:
    BoxDeviceStatus viewAllDeviceStatus() override { return BoxDeviceStatus(); }
    CameraRealTimeFrame getCameraRealTimeFrame(const CameraInfo&) override { return CameraRealTimeFrame(); }
    CameraHistoryVideo viewCameraHistoryVideo(const CameraInfo&) override { return CameraHistoryVideo(); }
    bool forceFxM(const std::string&, int, bool) override { return false; }
    bool forceFxX(const std::string&, int, bool) override { return false; }
    std::vector<FxPlcStatus> getChangedFxPlcStatus() override { return {}; }
    void clearChangedFxPlcStatus(const std::vector<std::string>&) override {}
    void acquisitionOnce() override {}
    bool readPointNow(const std::string&, const std::string&, int& value, std::string&) override {
        value = 0;
        return false;
    }
};

HttpRequest post(const std::string& path, const std::string& body,
                 const std::string& cookie = "", const std::string& clientIp = "192.168.1.100") {
    HttpRequest request;
    request.method = "POST";
    request.path = path;
    request.body = body;
    request.cookie = cookie;
    request.clientIp = clientIp;
    return request;
}

// 调控制器并把异常也纳入判定:异常一旦逃到 HTTP 工作线程就是 std::terminate,
// 整个盒子进程(PLC 采集 + 云端链路 + 正在进行的洗车)一起断。
// 所以"不抛异常"本身就是这条用例要守的东西。
HttpResponse call(AdminController& controller, const HttpRequest& request, bool& threw) {
    threw = false;
    try {
        return controller.handle(request);
    } catch (...) {
        threw = true;
        return HttpResponse{};
    }
}

std::string tokenFrom(const HttpResponse& response) {
    const std::string key = "washcar_session=";
    for (const auto& header : response.headers) {
        const std::size_t pos = header.find(key);
        if (pos == std::string::npos) continue;
        const std::size_t begin = pos + key.size();
        const std::size_t end = header.find(';', begin);
        return header.substr(begin, (end == std::string::npos ? header.size() : end) - begin);
    }
    return {};
}

}  // namespace

int main() {
    // 初始化最小日志(避免 LOG_WARNING 拿到 null logger)
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    logger->addSink(std::make_shared<ConsoleSink>());
    logger->setLogLevel(LogLevel::WARNING);
    logger->start();
    LoggerManager::instance().registerLogger("work", logger);

    // =====================================================================
    // 1. 配置读写与校验
    // =====================================================================

    // --- 1.1 文件不存在 → 返回 true, 生成模板, 字段全是默认值 ---
    {
        const std::string path = "/tmp/__washcar_not_exists__.json";
        std::remove(path.c_str());   // 确保不存在

        std::string err;
        ConfigManager::instance().applyDefaults();    // 重置基线
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.1 file not found → load returns true");
        expect(ConfigManager::instance().box().boxNo == "123456",
               "1.1 box.boxNo falls back to default '123456'");
        expect(ConfigManager::instance().mqtt().brokerIp == "116.204.32.197",
               "1.1 mqtt.brokerIp falls back to default");
        expect(readFile(path) == default_config::toJson(),
               "1.1 模板文件被生成, 内容跟 toJson() 一致");
        std::remove(path.c_str());
    }

    // --- 1.2 文件存在但为空 → 覆盖生成模板 ---
    {
        const std::string path = "/tmp/__washcar_empty__.json";
        { std::ofstream out(path); out.close(); }   // 写空文件

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.2 empty file → load returns true");
        expect(readFile(path) == default_config::toJson(),
               "1.2 空文件被覆盖成模板, 内容跟 toJson() 一致");
        std::remove(path.c_str());
    }

    // --- 1.3 JSON 解析失败 → 返回 false, 原文件不被改, 字段回退默认值 ---
    {
        const std::string path = "/tmp/__washcar_bad_json__.json";
        writeTempJson(path, "{ this is not valid json");
        const std::string contentBefore = readFile(path);

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        expect(!ok, "1.3 parse failure → load returns false");
        expect(readFile(path) == contentBefore,
               "1.3 解析失败时文件内容没被改(防止运维改坏的 JSON 被覆盖)");
        expect(ConfigManager::instance().box().boxNo == "123456",
               "1.3 解析失败后字段回退默认值");
        std::remove(path.c_str());
    }

    // --- 1.4 box.boxNo 缺失 → 返回 false, 错误信息点名字段 ---
    {
        std::string err;
        const std::string path = "/tmp/__washcar_no_boxno__.json";
        writeTempJson(path, R"({"mqtt": {"brokerIp": "1.2.3.4"}})");
        bool ok = ConfigManager::instance().load(path, err);
        expect(!ok, "1.4 box.boxNo missing → load returns false");
        expect(err.find("box.boxNo") != std::string::npos,
               "1.4 error message mentions box.boxNo");
        std::remove(path.c_str());
    }

    // --- 1.5 mqtt.brokerIp 缺失 → 返回 false, 错误信息点名字段 ---
    {
        std::string err;
        const std::string path = "/tmp/__washcar_no_broker__.json";
        writeTempJson(path, R"({"box": {"boxNo": "abc"}})");
        bool ok = ConfigManager::instance().load(path, err);
        expect(!ok, "1.5 mqtt.brokerIp missing → load returns false");
        expect(err.find("mqtt.brokerIp") != std::string::npos,
               "1.5 error message mentions mqtt.brokerIp");
        std::remove(path.c_str());
    }

    // --- 1.6 正常 JSON → 解析生效, 文件不被改动 ---
    {
        const std::string path = "/tmp/__washcar_normal__.json";
        const std::string body = R"({
            "box":   {"boxNo": "xyz999"},
            "mqtt":  {"brokerIp": "10.0.0.1", "port": 1884}
        })";
        writeTempJson(path, body);

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.6 valid JSON → load returns true");
        expect(ConfigManager::instance().box().boxNo == "xyz999", "1.6 boxNo parsed from JSON");
        expect(ConfigManager::instance().mqtt().brokerIp == "10.0.0.1", "1.6 brokerIp parsed from JSON");
        expect(ConfigManager::instance().mqtt().port == 1884, "1.6 port parsed from JSON");
        expect(readFile(path) == body, "1.6 正常加载时文件内容不变(端口没被覆盖成默认 1883)");
        std::remove(path.c_str());
    }

    // --- 1.7 machine 段的三种情况: 没有 / 是当前机型 / 是别的机型 ---
    {
        std::string err;
        const std::string path = "/tmp/__washcar_points__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.7 config without machine section loads ok");
        expect(ConfigManager::instance().machine().machineType == "haiTian",
               "1.7 缺 machine 段时用内置点位表");
        std::remove(path.c_str());
    }
    {
        std::string err;
        const std::string path = "/tmp/__washcar_points2__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "machine": {
                "haiTian": {
                    "machineType": "haiTian",
                    "protocol": "fxSerial",
                    "acquisition": {
                        "D": ["D142", "D150"]
                    },
                    "signals": [
                        {"name": "customSignal", "point": "D150", "report": "property"}
                    ]
                }
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.7 config with machine section loads ok");
        const auto& m = ConfigManager::instance().machine();
        expect(m.machineType == "haiTian", "1.7 machine section selected by boxType");
        expect(findSignal(m, "customSignal") != nullptr, "1.7 signal from config is in effect");
        std::remove(path.c_str());
    }
    {
        std::string err;
        const std::string path = "/tmp/__washcar_points3__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc", "boxType": "haiTian"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "machine": {
                "yiGongLi": {
                    "machineType": "yiGongLi",
                    "protocol": "modbusTcp",
                    "signals": [
                        {"name": "otherMachineSignal", "point": "D1000", "report": "event"}
                    ]
                }
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.7 config with other machine section loads ok");
        const auto& m = ConfigManager::instance().machine();
        expect(m.machineType == "haiTian", "1.7 boxType=haiTian still uses haiTian table");
        expect(findSignal(m, "otherMachineSignal") == nullptr,
               "1.7 other machine's signals are not mixed in");
        std::remove(path.c_str());
    }

    // --- 1.8 点位解析 / 格式化 ---
    {
        Point p = parsePoint("M26");
        expect(p.type == 'M' && p.addr == 26, "1.8 parsePoint('M26') == {'M', 26}");
        p = parsePoint("XYZ");
        expect(p.type == '?' && p.addr == -1, "1.8 parsePoint('XYZ') == {'?', -1}");
        expect(formatPoint({'M', 26}) == "M26", "1.8 formatPoint({'M', 26}) == 'M26'");
    }

    // --- 1.9 timing.* 三类上报轮询间隔可配化 ---
    {
        std::string err;
        const std::string path = "/tmp/__washcar_timing_defaults__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.9 JSON missing timing.* → load ok");
        expect(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 60000,
               "1.9 dailyReportCheckIntervalMs defaults to 60000");
        expect(ConfigManager::instance().timing().faultReportIntervalMs == 3000,
               "1.9 faultReportIntervalMs defaults to 3000");
        expect(ConfigManager::instance().timing().valueChangeIntervalMs == 3000,
               "1.9 valueChangeIntervalMs defaults to 3000");
        expect(ConfigManager::instance().timing().acquisitionIntervalMs == 500,
               "1.9 acquisitionIntervalMs defaults to 500");
        std::remove(path.c_str());
    }
    {
        std::string err;
        const std::string path = "/tmp/__washcar_timing_set__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "timing": {
                "dailyReportCheckIntervalMs": 30000,
                "faultReportIntervalMs":      1500,
                "valueChangeIntervalMs":      500,
                "acquisitionIntervalMs":      200
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.9 JSON sets timing.* → load ok");
        expect(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 30000,
               "1.9 dailyReportCheckIntervalMs = 30000");
        expect(ConfigManager::instance().timing().faultReportIntervalMs == 1500,
               "1.9 faultReportIntervalMs = 1500");
        expect(ConfigManager::instance().timing().valueChangeIntervalMs == 500,
               "1.9 valueChangeIntervalMs = 500");
        expect(ConfigManager::instance().timing().acquisitionIntervalMs == 200,
               "1.9 acquisitionIntervalMs = 200");
        std::remove(path.c_str());
    }
    {
        std::string err;
        const std::string path = "/tmp/__washcar_timing_clamp__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "timing": {
                "dailyReportCheckIntervalMs": 50,
                "faultReportIntervalMs":      10,
                "valueChangeIntervalMs":      1,
                "acquisitionIntervalMs":      5
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.9 tiny timing.* → load still ok (clamped, not fatal)");
        expect(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 1000,
               "1.9 dailyReportCheckIntervalMs clamped to 1000");
        expect(ConfigManager::instance().timing().faultReportIntervalMs == 100,
               "1.9 faultReportIntervalMs clamped to 100");
        expect(ConfigManager::instance().timing().valueChangeIntervalMs == 100,
               "1.9 valueChangeIntervalMs clamped to 100");
        expect(ConfigManager::instance().timing().acquisitionIntervalMs == 20,
               "1.9 acquisitionIntervalMs clamped to 20");
        std::remove(path.c_str());
    }

    // --- 1.10 startChecks: 覆盖 / 停用 / 追加 ---
    {
        std::string err;
        const std::string path = "/tmp/__washcar_startchecks__.json";
        writeTempJson(path, R"({
            "box":  {"boxNo": "abc"},
            "mqtt": {"brokerIp": "1.2.3.4"},
            "machine": {"haiTian": {"startChecks": [
                {"signal": "washPosition", "missing": "reject",
                 "reason": "custom reason", "suggestion": "custom suggestion"},
                {"signal": "startCondition", "enabled": false},
                {"signal": "stateRegister", "expect": 1, "missing": "allow",
                 "reason": "step not ready", "suggestion": "wait"}
            ]}}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.10 startChecks override → load ok");
        const auto& table = ConfigManager::instance().machine();
        const StartCheckConfig* washPosition = nullptr;
        const StartCheckConfig* startCondition = nullptr;
        const StartCheckConfig* added = nullptr;
        for (const auto& rule : table.startChecks) {
            if (rule.signal == "washPosition") washPosition = &rule;
            if (rule.signal == "startCondition") startCondition = &rule;
            if (rule.signal == "stateRegister") added = &rule;
        }
        expect(washPosition && washPosition->missing == "reject", "1.10 同 key 规则被覆盖");
        expect(washPosition && washPosition->reason == "custom reason", "1.10 覆盖后文案生效");
        expect(startCondition && !startCondition->enabled, "1.10 enabled=false 停用内置规则");
        expect(added && added->reason == "step not ready", "1.10 新规则追加");
        expect(table.startChecks.size() == 6, "1.10 规则数 = 内置 5 + 追加 1");
        std::remove(path.c_str());
    }

    // --- 1.11 坏规则 → 回退内置表(盒子照常起) ---
    // 两类坏规则各测一遍: missing 取值非法 / 压根没写 missing。
    // 共同结果: 不阻断启动, 回退到内置的 5 条规则。
    {
        // 坏规则一: missing 取值非法
        std::string err;
        const std::string path = "/tmp/__washcar_badrule__.json";
        writeTempJson(path, R"({
            "box":  {"boxNo": "abc"},
            "mqtt": {"brokerIp": "1.2.3.4"},
            "machine": {"haiTian": {"startChecks": [
                {"signal": "washPosition", "missing": "nope", "reason": "x"}
            ]}}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.11 坏规则(missing 取值非法)不阻断启动");
        const auto& table = ConfigManager::instance().machine();
        expect(table.startChecks.size() == 5, "1.11 回退内置规则(5 条)");
        bool restored = false;
        for (const auto& item : table.startChecks) {
            if (item.signal == "washPosition" && item.missing == "allow") restored = true;
        }
        expect(restored, "1.11 回退后 washPosition 仍是内置的 allow");
        std::remove(path.c_str());
    }
    {
        // 坏规则二: 压根没写 missing(missing 必须显式写)
        std::string err;
        const std::string path = "/tmp/__washcar_badrule__.json";
        writeTempJson(path, R"({
            "box":  {"boxNo": "abc"},
            "mqtt": {"brokerIp": "1.2.3.4"},
            "machine": {"haiTian": {"startChecks": [
                {"signal": "washPosition", "reason": "x"}
            ]}}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        expect(ok, "1.11 坏规则(缺 missing)不阻断启动");
        const auto& table = ConfigManager::instance().machine();
        expect(table.startChecks.size() == 5, "1.11 回退内置规则(5 条)");
        bool stillAllow = false;
        for (const auto& item : table.startChecks) {
            if (item.signal == "washPosition" && item.missing == "allow") stillAllow = true;
        }
        expect(stillAllow, "1.11 回退后仍是内置 allow");
        std::remove(path.c_str());
    }

    // =====================================================================
    // 2. 内置点位表自洽
    // =====================================================================
    {
        for (const char* boxType : {"haiTian", "yiGongLi"}) {
            std::string error;
            const auto& pointTable = default_point::getPointTable(boxType);
            expect(validatePointTable(pointTable, error),
                   std::string(boxType) + " 内置表校验通过: " + error);
            expect(pointTable.machineType == boxType, std::string(boxType) + " machineType 对得上");
        }
        expect(default_point::getPointTable("somethingNew").machineType == "haiTian", "2. 未知机型回退海天");

        const auto& haiTian = default_point::getPointTable("haiTian");
        const auto& yiGongLi = default_point::getPointTable("yiGongLi");

        // 事件点位集合 + 上报字段名必须跟改造前一致(D28 已改名 fault)
        {
            std::vector<std::string> eventPoints, eventFields, propertyFields;
            for (const auto& signal : haiTian.signals) {
                if (hasReport(signal.report, SignalReport::REPORT_PUSHALARM)) {
                    eventPoints.push_back(signal.point);
                    eventFields.push_back(cloudKeyOf(signal));
                } else if (hasReport(signal.report, SignalReport::REPORT_MONITOR)) {
                    propertyFields.push_back(cloudKeyOf(signal));
                }
            }
            std::sort(eventPoints.begin(), eventPoints.end());
            std::sort(eventFields.begin(), eventFields.end());
            std::sort(propertyFields.begin(), propertyFields.end());
            expect(eventPoints == std::vector<std::string>({"D28", "M257", "M259", "M381", "S0", "X44", "X50"}),
                   "2. 海天事件点位集合跟改造前一致");
            expect(eventFields == std::vector<std::string>({"M257", "M259", "M381", "S0", "X44", "X50", "fault"}),
                   "2. 海天故障上报字段名跟改造前一致");
            expect(propertyFields == std::vector<std::string>({"D142", "M1", "M187", "Y50", "Y51", "Y52", "Y74"}),
                   "2. 海天变化上报字段名跟改造前一致");
        }

        // 故障码:海天 D28 / 驿公里 D1000
        expect(findSignal(haiTian, "fault")->point == "D28", "2. 海天 fault 绑 D28");
        expect(cloudKeyOf(*findSignal(haiTian, "fault")) == "fault", "2. 海天故障码字段名是 fault");
        expect(findSignal(haiTian, "washing")->point == "Y74", "2. 海天 washing 绑 Y74");
        expect(cloudKeyOf(*findSignal(yiGongLi, "fault")) == "d1000", "2. 驿公里故障码字段名是 d1000");
        expect(!yiGongLi.faultCodes.empty(), "2. 驿公里表带故障码名称表");

        // 服务写点位
        expect(servicePointOf(haiTian, "start") == "X45", "2. 海天 start 写 X45");
        expect(servicePointOf(haiTian, "stop") == "M0", "2. 海天 stop 写 M0");
        expect(servicePointOf(haiTian, "reset") == "M100", "2. 海天 reset 写 M100");
        expect(servicePointOf(haiTian, "noSuchService").empty(), "2. 不存在的服务返回空串");
        expect(servicePointOf(yiGongLi, "start", "1") == "M521", "2. 驿公里 start 模式1 = M521");
        expect(servicePointOf(yiGongLi, "start", "3") == "M523", "2. 驿公里 start 模式3 = M523");
        expect(servicePointOf(yiGongLi, "start").empty(), "2. 驿公里 start 没有 default 条件");
        expect(servicePointOf(yiGongLi, "startPrepare") == "M204", "2. 驿公里 startPrepare 写 M204");
        expect(servicePointOf(yiGongLi, "reset") == "M202", "2. 驿公里 reset 写 M202");
        expect(servicePointOf(yiGongLi, "resetSafe") == "M203", "2. 驿公里 resetSafe 写 M203");
        expect(servicePointOf(yiGongLi, "abort") == "M206", "2. 驿公里 abort 写 M206");

        // 数量约定
        {
            int propertyCount = 0, eventCount = 0;
            for (const auto& signal : haiTian.signals) {
                if (hasReport(signal.report, SignalReport::REPORT_MONITOR)) ++propertyCount;
                else if (hasReport(signal.report, SignalReport::REPORT_PUSHALARM)) ++eventCount;
            }
            expect(propertyCount == 7, "2. 海天属性数 = 7 (实际 " + std::to_string(propertyCount) + ")");
            expect(eventCount == 7, "2. 海天事件数 = 7 (实际 " + std::to_string(eventCount) + ")");
            int yiPropertyCount = 0;
            for (const auto& signal : yiGongLi.signals) {
                if (hasReport(signal.report, SignalReport::REPORT_MONITOR)) ++yiPropertyCount;
            }
            expect(yiPropertyCount == 9, "2. 驿公里属性数 = 9 (实际 " + std::to_string(yiPropertyCount) + ")");
            expect(yiGongLi.acquisition.sPoints.size() == 82, "2. 驿公里 S 点 82 个");
        }

        // 7-13 现场验证过的那版没有归位自检这道门, 清单留空。
        expect(haiTian.homeSensors.empty(),
               "2. 海天归位自检已关 (实际 " + std::to_string(haiTian.homeSensors.size()) + ")");
        expect(findSignal(haiTian, "antiCollisionBarLeft")->point == "X62", "2. 左防撞杆绑 X62");
        expect(findSignal(haiTian, "antiCollisionBarRight")->point == "X63", "2. 右防撞杆绑 X63");
        expect(findSignal(haiTian, "vehicleHeightOk")->point == "X46", "2. 车辆高度检测绑 X46");

        // 校验规则要能挡住坏表
        {
            std::string error;
            auto broken = default_point::getPointTable("haiTian");
            broken.signals.push_back(broken.signals[0]);
            expect(!validatePointTable(broken, error), "2. 重名信号被挡下");
            expect(error.find("重复") != std::string::npos, "2. 重名提示里有'重复'");

            auto brokenRef = default_point::getPointTable("haiTian");
            brokenRef.faultSignals.push_back("noSuchSignal");
            expect(!validatePointTable(brokenRef, error), "2. 引用不存在的信号名被挡下");

            auto brokenFormat = default_point::getPointTable("haiTian");
            brokenFormat.acquisition.mPoints.push_back("Mxx");
            expect(!validatePointTable(brokenFormat, error), "2. 非法点位格式被挡下");

            auto brokenOrphan = default_point::getPointTable("haiTian");
            brokenOrphan.signals.push_back({"orphan", "M9999", SignalReport::REPORT_NONE, "", "没采集的点位"});
            expect(!validatePointTable(brokenOrphan, error), "2. 点位不在采集清单被挡下");
            expect(error.find("M9999") != std::string::npos, "2. 提示里带出孤儿点位");
        }
    }

    // =====================================================================
    // 3. 配置补丁: 校验 / 保存 / 回滚
    // =====================================================================
    {
        const std::string path = "/tmp/__washcar_patch__.json";
        std::filesystem::remove(path);
        std::filesystem::remove("/tmp/config.lastgood.json");
        std::filesystem::remove_all("/tmp/config_history");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << default_config::toJson();
        }

        auto& mgr = ConfigManager::instance();
        std::string error;
        expect(mgr.load(path, error), "3. 模板加载成功");

        {
            nlohmann::json merged;
            expect(!mgr.validatePatch(nlohmann::json{{"mqtt", {{"topics", {{"monitor", "no-boxno"}}}}}},
                                      mgr.revision(), merged, error),
                   "3. 缺 {boxNo} 的主题被拒");
        }
        {
            nlohmann::json merged;
            const nlohmann::json patch =
                nlohmann::json{{"mqtt", {{"topics", {{"monitor", "washcar.{boxNo}.state"}}}}}};
            error.clear();
            expect(mgr.validatePatch(patch, mgr.revision(), merged, error), "3. 点号主题可校验");
            std::string revision;
            error.clear();
            expect(mgr.savePatch(patch, mgr.revision(), "tester", "127.0.0.1", error, revision),
                   "3. 点号主题可保存");
            expect(!revision.empty(), "3. 返回新 revision");
        }
        {
            nlohmann::json signal = {{"name", "waterPressure"}, {"point", "D150"},
                                     {"report", nlohmann::json::array({"monitor", "readdata"})},
                                     {"cloudKey", "D150"}, {"normalValue", 0}, {"comment", "水压"}};
            nlohmann::json patch = {{"machine", {{"haiTian", {{"signals",
                                     nlohmann::json::array({signal})}}}}}};
            std::string revision;
            error.clear();
            expect(mgr.savePatch(patch, mgr.revision(), "tester", "127.0.0.1", error, revision),
                   "3. 自定义点位可保存");
            error.clear();
            expect(mgr.load(path, error), "3. 自定义点位后可重载");
            const auto& table = mgr.machine();
            bool found = false;
            for (const auto& item : table.signals) if (item.name == "waterPressure") found = true;
            expect(found, "3. 自定义点位落盘");
            bool acquired = false;
            for (const auto& point : table.acquisition.dPoints) if (point == "D150") acquired = true;
            expect(acquired, "3. 自定义点位自动进采集清单");
            expect(table.signals.size() > default_point::getPointTable("haiTian").signals.size(),
                   "3. 内置点位保留并追加");
        }
        {
            auto entries = mgr.history();
            expect(!entries.empty(), "3. 有历史版本");
            if (!entries.empty()) {
                std::string revision;
                error.clear();
                expect(mgr.rollback(entries.back().id, "tester", "127.0.0.1", error, revision),
                       "3. 回滚成功");
                error.clear();
                expect(mgr.load(path, error), "3. 回滚后可重载");
                expect(mgr.mqtt().topics.monitor == "flexem/{boxNo}/monitor",
                       "3. 回滚恢复默认主题");
            }
        }

        std::filesystem::remove(path);
        std::filesystem::remove("/tmp/config.lastgood.json");
        std::filesystem::remove_all("/tmp/config_history");
    }

    // =====================================================================
    // 4. 配置台健壮性: 畸形请求不能把盒子进程带崩
    //
    // 之前 json::value() 取字段,类型不符会抛 type_error;异常从 HTTP 工作
    // 线程逃出去就是 std::terminate,整个盒子进程 abort —— PLC 采集、云端链路、
    // 正在进行的洗车一起断。一条畸形请求就能停掉一台机器。
    //
    // 这里不测鉴权、密码策略那些,那些跟"别让盒子崩"无关。
    // =====================================================================
    {
        const std::string path = "/tmp/washcar_config_console_robustness.json";
        const std::string lastGood = "/tmp/config.lastgood.json";
        const std::string history = "/tmp/config_history";
        std::filesystem::remove(path);
        std::filesystem::remove(lastGood);
        std::filesystem::remove_all(history);
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << default_config::toJson();
        }

        auto& mgr = ConfigManager::instance();
        std::string error;
        expect(mgr.load(path, error), "4. load config");

        StubLobbyService lobby;
        StubDeviceService device;
        AuthService auth;               // 必须在 load 之后构造:它读的是配置里的密码哈希
        AdminController controller(lobby, device, auth, "/tmp");

        // 4.1 字段类型不对必须回 400,而且不能抛异常出去。
        //     还要点名是哪个字段:只回 400 分不清是"取字段时做了类型检查"还是
        //     "被兜底的异常处理接住了" —— 后者只是不崩,没告诉现场哪里填错了。
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/login", R"({"username":1,"password":"x"})"), threw);
            expect(!threw, "4.1 type-confused username does not throw");
            expect(!threw && response.status == 400, "4.1 type-confused username returns 400");
            expect(response.body.find("username") != std::string::npos,
                   "4.1 type-confused username names the offending field");
        }
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/login", R"({"username":"admin","password":123})"), threw);
            expect(!threw, "4.1 type-confused password does not throw");
            expect(!threw && response.status == 400, "4.1 type-confused password returns 400");
            expect(response.body.find("password") != std::string::npos,
                   "4.1 type-confused password names the offending field");
        }
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/login", R"({"username":{"a":1},"password":["x"]})"), threw);
            expect(!threw, "4.1 nested type-confused fields do not throw");
            expect(!threw && response.status == 400, "4.1 nested type-confused fields return 400");
        }
        {
            // 顶层不是对象(数组)也得出一个正常应答,不能抛。
            bool threw = false;
            const HttpResponse response = call(controller, post("/api/login", R"([1,2,3])"), threw);
            expect(!threw, "4.1 array body does not throw");
        }
        {
            // 非法 JSON 本来就该被 parseBody 挡下,这里确认它也不抛。
            bool threw = false;
            const HttpResponse response = call(controller, post("/api/login", "{not json"), threw);
            expect(!threw, "4.1 malformed json does not throw");
            expect(!threw && response.status == 400, "4.1 malformed json returns 400");
        }

        // 4.2 登录拿一个会话(后面几条要登录态)。
        std::string token;
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/login", R"({"username":"admin","password":""})"), threw);
            expect(!threw && response.status == 200, "4.2 default password login succeeds");
            token = tokenFrom(response);
            expect(!token.empty(), "4.2 session token issued");
        }
        const std::string cookie = "washcar_session=" + token;

        // 4.3 登录后的路径同样不许被畸形参数打崩。
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/logs", R"({"limit":"abc"})", cookie), threw);
            expect(!threw, "4.3 type-confused limit does not throw");
            expect(!threw && response.status == 400, "4.3 type-confused limit returns 400");
        }
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/plc/test-read", R"({"point":123})", cookie), threw);
            expect(!threw, "4.3 type-confused point does not throw");
            expect(!threw && response.status == 400, "4.3 type-confused point returns 400");
        }
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/config/rollback", R"({"id":[1,2]})", cookie), threw);
            expect(!threw, "4.3 type-confused rollback id does not throw");
            expect(!threw && response.status == 400, "4.3 type-confused rollback id returns 400");
        }
        {
            bool threw = false;
            const HttpResponse response =
                call(controller, post("/api/config/apply", R"({"expectedRevision":5})", cookie), threw);
            expect(!threw, "4.3 type-confused expectedRevision does not throw");
            expect(!threw && response.status == 400, "4.3 type-confused expectedRevision returns 400");
        }

        std::filesystem::remove(path);
        std::filesystem::remove(lastGood);
        std::filesystem::remove_all(history);
    }

    // =====================================================================
    // 5. 日志查询(配置台日志页的数据源)
    // =====================================================================
    {
        const std::string dir = "/tmp/washcar_log_query_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        {
            std::ofstream out(dir + "/log_2026-10-22.txt");
            out << "[2026-10-22 10:00:00] [INFO] [T:1] [LobbyService] start ok\n";
            out << "[2026-10-22 10:00:01] [ERROR] [T:2] [MqttService] mqtt error\n";
            out << "[2026-10-22 10:00:02] [WARNING] [T:3] [FxDeviceAcquisitionTask] plc timeout\n";
            out << "[2026-10-22 10:00:03] [INFO] [T:4] [ConfigUI] login\n";
            out << "raw line without format\n";
        }

        auto all = log_query::query(dir, "", "all", "", "", 100);
        expect(all.dates.size() == 1 && all.dates[0] == "2026-10-22", "5. date listed");
        expect(all.entries.size() == 5, "5. all lines");
        expect(all.counts["lobby"] == 1 && all.counts["mqtt"] == 1 &&
                   all.counts["plc"] == 1 && all.counts["config"] == 1 &&
                   all.counts["system"] == 1,
               "5. service counts");

        auto mqtt = log_query::query(dir, "", "mqtt", "", "", 100);
        expect(mqtt.entries.size() == 1 && mqtt.entries[0].message.find("mqtt error") != std::string::npos,
               "5. service filter");
        expect(mqtt.total == 5, "5. total before service filter");

        auto errors = log_query::query(dir, "", "all", "ERROR", "", 100);
        expect(errors.entries.size() == 1 && errors.entries[0].service == "mqtt", "5. level filter");

        auto keyword = log_query::query(dir, "", "all", "", "timeout", 100);
        expect(keyword.entries.size() == 1 && keyword.entries[0].service == "plc", "5. keyword filter");

        auto limited = log_query::query(dir, "", "all", "", "", 2);
        expect(limited.entries.size() == 2, "5. limit");
        expect(limited.entries.back().message.find("raw line") != std::string::npos, "5. limit keeps tail");

        std::filesystem::remove_all(dir);
    }

    std::cout << "config_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
