// config_manager_test.cpp — 覆盖 spec §十二 单元测试清单的 10 个场景
// 用法:编译后直接跑 ./config_manager_test,无需命令行参数
// 退出码:0 = 全过,非 0 = 失败用例数
#include "common/config/config_manager.h"
#include "common/config/config_object.h"
#include "common/config/point.h"
#include "common/config/default_config.cpp"   // 测试需要 default_config::toJson() 比对模板内容
#include "common/log/log_manager.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cassert>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, desc) do {                                              \
    if (cond) { ++g_pass; std::cout << "  [PASS] " << desc << std::endl; }   \
    else      { ++g_fail; std::cout << "  [FAIL] " << desc << std::endl; }   \
} while (0)

static const washcar::cfg::SignalConfig* findSignal(const washcar::cfg::MachinePointTable& table,
                                                     const std::string& name) {
    for (const auto& signal : table.signals) {
        if (signal.name == name) return &signal;
    }
    return nullptr;
}

static void writeTempJson(const std::string& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

int main() {
    // 初始化最小日志(避免 LOG_WARNING 拿到 null logger)
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    logger->addSink(std::make_shared<ConsoleSink>());
    logger->setLogLevel(LogLevel::WARNING);
    logger->start();
    LoggerManager::instance().registerLogger("work", logger);

    std::cout << "========== config_manager_test ==========" << std::endl;

    // ---------- 用例 1:文件不存在 → 返回 true,字段全是默认值 ----------
    {
        std::string err;
        ConfigManager::instance().applyDefaults();    // 重置基线
        bool ok = ConfigManager::instance().load("/tmp/__washcar_config_does_not_exist__.json", err);
        CHECK(ok, "1. file not found → load returns true");
        CHECK(ConfigManager::instance().box().boxNo == "123456",
              "1. box.boxNo falls back to default '123456'");
        CHECK(ConfigManager::instance().mqtt().brokerIp == "116.204.32.197",
              "1. mqtt.brokerIp falls back to default");
    }

    // ---------- 用例 2:JSON 解析失败(花括号不匹配) → 返回 false,字段回退默认值 ----------
    {
        std::string err;
        const std::string path = "/tmp/__washcar_bad_json__.json";
        writeTempJson(path, "{ this is not valid json");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(!ok, "2. parse failure → load returns false");
        CHECK(ConfigManager::instance().box().boxNo == "123456",
              "2. fields fall back to default after parse failure");
        std::remove(path.c_str());
    }

    // ---------- 用例 3:box.boxNo 缺失 → 返回 false ----------
    {
        std::string err;
        const std::string path = "/tmp/__washcar_no_boxno__.json";
        writeTempJson(path, R"({"mqtt": {"brokerIp": "1.2.3.4"}})");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(!ok, "3. box.boxNo missing → load returns false");
        CHECK(err.find("box.boxNo") != std::string::npos,
              "3. error message mentions box.boxNo");
        std::remove(path.c_str());
    }

    // ---------- 用例 4:mqtt.brokerIp 缺失 → 返回 false ----------
    {
        std::string err;
        const std::string path = "/tmp/__washcar_no_broker__.json";
        writeTempJson(path, R"({"box": {"boxNo": "abc"}})");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(!ok, "4. mqtt.brokerIp missing → load returns false");
        CHECK(err.find("mqtt.brokerIp") != std::string::npos,
              "4. error message mentions mqtt.brokerIp");
        std::remove(path.c_str());
    }

// ---------- 用例 7:json 没写 machine 段时用内置点位表 ----------
    {
        std::string err;
        const std::string path = "/tmp/__washcar_points__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "7. config without machine section loads ok");
        CHECK(ConfigManager::instance().machine().machineType == "haiTian",
              "7. built-in machine profile used when machine section absent");
        std::remove(path.c_str());
    }

    // ---------- 用例 7b:json 写了 machine.<boxType> 段时用配置里那份 ----------
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
                        "intervalMs": 1000,
                        "D": ["D142"]
                    },
                    "signals": [
                        {"name": "customSignal", "point": "D142", "report": "property"}
                    ]
                }
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "7b. config with machine section loads ok");
        const auto& m = ConfigManager::instance().machine();
        CHECK(m.machineType == "haiTian", "7b. machine section selected by boxType");
        CHECK(findSignal(m, "customSignal") != nullptr, "7b. signal from config is in effect");
        std::remove(path.c_str());
    }

    // ---------- 用例 7c:其它机型的段不影响当前机型 ----------
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
        CHECK(ok, "7c. config with other machine section loads ok");
        const auto& m = ConfigManager::instance().machine();
        CHECK(m.machineType == "haiTian", "7c. boxType=haiTian still uses haiTian table");
        CHECK(findSignal(m, "otherMachineSignal") == nullptr,
              "7c. other machine's signals are not mixed in");
        std::remove(path.c_str());
    }
    {
        washcar::cfg::Point p = washcar::cfg::parsePoint("M26");
        CHECK(p.type == 'M' && p.addr == 26, "8. parsePoint('M26') == {'M', 26}");
    }

    // ---------- 用例 9:parsePoint("XYZ") → {'?', -1} ----------
    {
        washcar::cfg::Point p = washcar::cfg::parsePoint("XYZ");
        CHECK(p.type == '?' && p.addr == -1, "9. parsePoint('XYZ') == {'?', -1}");
    }

    // ---------- 用例 10:formatPoint({'M', 26}) → "M26" ----------
    {
        std::string s = washcar::cfg::formatPoint({'M', 26});
        CHECK(s == "M26", "10. formatPoint({'M', 26}) == 'M26'");
    }

    // ---------- §16.5 模板生成 4 情况 ----------
    // A. 文件不存在 → 模板被生成 + 内容跟 toJson() 一致 + 返回 true
    {
        const std::string path = "/tmp/__washcar_A_not_exists__.json";
        std::remove(path.c_str());   // 确保不存在

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "A. file not exists → load returns true");

        // 文件被创建
        std::ifstream in(path);
        CHECK(in.good(), "A. template file created on disk");
        std::stringstream ss; ss << in.rdbuf();
        std::string onDisk = ss.str();
        in.close();

        // 内容跟 toJson() 输出 1 字节不差
        CHECK(onDisk == default_config::toJson(),
              "A. on-disk content == toJson()");

        std::remove(path.c_str());
    }

    // B. 文件存在但 size==0 → 覆盖生成模板
    {
        const std::string path = "/tmp/__washcar_B_empty__.json";
        std::ofstream out(path); out.close();   // 写空文件

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "B. empty file → load returns true");

        std::ifstream in(path);
        std::stringstream ss; ss << in.rdbuf();
        std::string onDisk = ss.str();
        in.close();
        CHECK(!onDisk.empty(), "B. file no longer empty");
        CHECK(onDisk == default_config::toJson(),
              "B. on-disk content == toJson()");

        std::remove(path.c_str());
    }

    // C. JSON 语法错 → 文件**没被改** + 返回 false + 走默认值
    {
        const std::string path = "/tmp/__washcar_C_badjson__.json";
        const std::string original = "{ this is not valid json";
        writeTempJson(path, original);

        // 算原始 md5(简化:只比较 size + 内容)
        std::ifstream inBefore(path, std::ios::binary | std::ios::ate);
        std::streamsize sizeBefore = inBefore.tellg();
        inBefore.seekg(0);
        std::stringstream b; b << inBefore.rdbuf();
        std::string contentBefore = b.str();
        inBefore.close();

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(!ok, "C. parse failure → load returns false");

        // 文件内容没被改(防止运维改坏的 JSON 被覆盖)
        std::ifstream inAfter(path, std::ios::binary | std::ios::ate);
        std::streamsize sizeAfter = inAfter.tellg();
        inAfter.seekg(0);
        std::stringstream a; a << inAfter.rdbuf();
        std::string contentAfter = a.str();
        inAfter.close();
        CHECK(sizeBefore == sizeAfter && contentBefore == contentAfter,
              "C. file content unchanged on parse failure");

        // 字段已回退到默认值
        CHECK(ConfigManager::instance().box().boxNo == "123456",
              "C. fields fall back to defaults");

        std::remove(path.c_str());
    }

    // D. 正常 → 不动文件 + 解析生效
    {
        const std::string path = "/tmp/__washcar_D_normal__.json";
        const std::string body = R"({
            "box":   {"boxNo": "xyz999"},
            "mqtt":  {"brokerIp": "10.0.0.1", "port": 1884}
        })";
        writeTempJson(path, body);

        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "D. valid JSON → load returns true");
        CHECK(ConfigManager::instance().box().boxNo == "xyz999",
              "D. boxNo parsed from JSON");
        CHECK(ConfigManager::instance().mqtt().brokerIp == "10.0.0.1",
              "D. brokerIp parsed from JSON");
        CHECK(ConfigManager::instance().mqtt().port == 1884,
              "D. port parsed from JSON");

        // 文件未被改动(原 1884 端口没被覆盖成默认 1883)
        std::ifstream in(path);
        std::stringstream ss; ss << in.rdbuf();
        std::string onDisk = ss.str();
        in.close();
        CHECK(onDisk == body, "D. file content unchanged on normal load");

        std::remove(path.c_str());
    }

    // ---------- §S-5(2026-07-10)3 类上报轮询间隔可配化 ----------
    // E. JSON 缺失 timing.* → 用默认 60000/3000/3000
    {
        std::string err;
        const std::string path = "/tmp/__washcar_E_defaults__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"}
        })");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "E. JSON missing timing.* → load ok");
        CHECK(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 60000,
              "E. dailyReportCheckIntervalMs defaults to 60000");
        CHECK(ConfigManager::instance().timing().faultReportIntervalMs == 3000,
              "E. faultReportIntervalMs defaults to 3000");
        CHECK(ConfigManager::instance().timing().valueChangeIntervalMs == 3000,
              "E. valueChangeIntervalMs defaults to 3000");
        std::remove(path.c_str());
    }

    // F. JSON 设值生效
    {
        std::string err;
        const std::string path = "/tmp/__washcar_F_set__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "timing": {
                "dailyReportCheckIntervalMs": 30000,
                "faultReportIntervalMs":      1500,
                "valueChangeIntervalMs":      500
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "F. JSON sets timing.* → load ok");
        CHECK(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 30000,
              "F. dailyReportCheckIntervalMs = 30000");
        CHECK(ConfigManager::instance().timing().faultReportIntervalMs == 1500,
              "F. faultReportIntervalMs = 1500");
        CHECK(ConfigManager::instance().timing().valueChangeIntervalMs == 500,
              "F. valueChangeIntervalMs = 500");
        std::remove(path.c_str());
    }

    // G. 越界值 clamp(fault/valueChange <100ms 强制 100ms,daily <1000ms 强制 1000ms)
    {
        std::string err;
        const std::string path = "/tmp/__washcar_G_clamp__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "timing": {
                "dailyReportCheckIntervalMs": 50,
                "faultReportIntervalMs":      10,
                "valueChangeIntervalMs":      1
            }
        })");
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "G. tiny timing.* → load still ok (clamped, not fatal)");
        CHECK(ConfigManager::instance().timing().dailyReportCheckIntervalMs == 1000,
              "G. dailyReportCheckIntervalMs clamped to 1000");
        CHECK(ConfigManager::instance().timing().faultReportIntervalMs == 100,
              "G. faultReportIntervalMs clamped to 100");
        CHECK(ConfigManager::instance().timing().valueChangeIntervalMs == 100,
              "G. valueChangeIntervalMs clamped to 100");
        std::remove(path.c_str());
    }

    std::cout << "========== " << g_pass << " passed, " << g_fail << " failed ==========" << std::endl;
    return g_fail == 0 ? 0 : 1;
}