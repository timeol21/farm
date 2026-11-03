// outbox 端到端场景测试：用发送替身替代真实 socket，覆盖任务文档第 31-38 行点名的 6 个场景。
//
// 替身注入点：MqttService::setSendOverrideForTest（发送成功/失败可控）
//             MqttService::setConnectedForTest（模拟断网/恢复）
//             MqttService::flushBufferedForTest（手动触发补发）
// 三个入口生产代码都不调用，默认走真实 socket，行为不变。
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "business_layer/command/controller.h"
#include "presentation_layer/http_response.h"
#include "common/sqlite/database_manager.h"
#include "common/config/config_manager.h"

#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <vector>

// controller 在 outbox 测试里用不到（我们从不路由收到的消息），给个空壳即可。
// 这样不用把整个大厅服务链进测试。
class NullController : public IController {
public:
    HttpResponse handle(const std::string&, const std::string&) override { return HttpResponse(); }
    void handleMqtt(const std::string&, const std::string&) override {}
};

using MsgType = MessageBuffer::MessageType;
static const MsgType FAULT = MessageBuffer::MessageType::TYPE_FAULT_REPORT;
static const MsgType STATUS = MessageBuffer::MessageType::TYPE_WASHCAR_STATUS;
// 命令结果类不参与"只留最新",每条都保留
static const MsgType RESULT = MessageBuffer::MessageType::TYPE_POINT_RESULT;

static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; \
    std::cerr << "FAIL: " << #x << " @" << __LINE__ << "\n"; } } while (0)

// 发送记录：topic 后半段 -> 收到的 payload 顺序
static std::vector<std::pair<std::string, std::string>> g_sent;
static bool g_sendShouldFail = false;

static int count(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    int n = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK
        && sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

// 从库里按 id 升序读出所有未发消息的 payload，用来验证顺序和条数
static std::vector<std::string> unsentPayloads(sqlite3* db) {
    std::vector<std::string> out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT payload FROM mqtt_messages WHERE sent=0 ORDER BY id", -1, &stmt, nullptr)
        != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return out;
}

static void resetAll(sqlite3* db) {
    sqlite3_exec(db, "DELETE FROM mqtt_messages;", nullptr, nullptr, nullptr);
    MessageBuffer::getInstance().clear();
    g_sent.clear();
    g_sendShouldFail = false;
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "washcar_outbox_scenarios";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    DatabaseManager& db = DatabaseManager::instance();
    if (!db.init(dir.string())) {
        std::cerr << "数据库初始化失败" << std::endl;
        return 2;
    }
    sqlite3* raw = db.getCommandDB();
    auto& buffer = MessageBuffer::getInstance();
    buffer.clear();
    buffer.setDatabase(raw);
    buffer.setLogger([](const std::string&) {});   // 静音，测试输出更干净

    // 替身：记录所有"发出去"的消息，可控制成败
    NullController controller;
    MqttProtocol protocol;
    MqttService service("127.0.0.1", 1883, "u", "p", "testbox", controller, protocol);
    service.setSendOverrideForTest([](const std::string& topic, const std::string& payload) {
        if (g_sendShouldFail) return false;
        g_sent.emplace_back(topic, payload);
        return true;
    });

    // ---------- 场景 1：在线直发，库里不留未发记录 ----------
    {
        resetAll(raw);
        service.setConnectedForTest(true);
        service.publishWithBuffer("event/post", "{\"fault\":1}", FAULT);
        CHECK(g_sent.size() == 1);                                   // 确实发出去了
        CHECK(g_sent[0].second == "{\"fault\":1}");
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0);  // 库里没有积压
    }

    // ---------- 场景 2：发送失败后入库，下轮补发并按 id 标记 ----------
    {
        resetAll(raw);
        service.setConnectedForTest(true);
        g_sendShouldFail = true;                                     // 模拟 socket 写失败
        service.publishWithBuffer("event/post", "{\"fault\":2}", FAULT);
        g_sendShouldFail = false;
        CHECK(g_sent.empty());                                        // 没发出去
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 1);  // 进库了

        service.handleReconnected();                                  // 模拟重连：把库里的装进内存队列
        service.flushBufferedForTest(10);                             // 补发
        CHECK(g_sent.size() == 1);
        CHECK(g_sent[0].second == "{\"fault\":2}");
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0);  // 补发后标记完成
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=1") == 1);
    }

    // ---------- 场景 3：离线 3 条故障全部入库，重连后只补发最新一条 ----------
    {
        resetAll(raw);
        service.setConnectedForTest(false);                          // 断网
        service.publishWithBuffer("event/post", "fault-A", FAULT);
        service.publishWithBuffer("event/post", "fault-B", FAULT);
        service.publishWithBuffer("event/post", "fault-C", FAULT);
        CHECK(g_sent.empty());
        // 入库时不合并：断网期间发生了什么就如实存了什么
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 3);

        service.setConnectedForTest(true);
        service.handleReconnected();
        // 重连时收敛：旧的直接删除,只留最新一条待补发
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages") == 1);

        service.flushBufferedForTest(10);
        CHECK(g_sent.size() == 1);
        CHECK(g_sent[0].second == "fault-C");
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0);
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=1") == 1);
    }

    // ---------- 场景 4：状态类离线期间变化 5 次，重连后只发最新一条 ----------
    {
        resetAll(raw);
        service.setConnectedForTest(false);
        for (int i = 1; i <= 5; ++i) {
            service.publishWithBuffer("monitor", "{\"step\":" + std::to_string(i) + "}", STATUS);
        }
        const auto kept = unsentPayloads(raw);
        CHECK(kept.size() == 1);                                     // 只留最新
        CHECK(kept[0] == "{\"step\":5}");

        service.setConnectedForTest(true);
        service.handleReconnected();
        service.flushBufferedForTest(10);
        CHECK(g_sent.size() == 1);
        CHECK(g_sent[0].second == "{\"step\":5}");
    }

    // ---------- 场景 5：重启进程后，库里未发消息仍能补发 ----------
    {
        resetAll(raw);
        service.setConnectedForTest(false);
        service.publishWithBuffer("event/post", "survive-restart", RESULT);
        service.publishWithBuffer("event/post", "survive-restart-2", RESULT);
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 2);

        // 模拟进程重启：内存队列全丢（handleConnectionLost 把内存里的存回库后 buffer 空），
        // 只剩数据库这一个事实来源。
        service.handleConnectionLost();
        MessageBuffer::getInstance().clear();

        service.setConnectedForTest(true);
        service.handleReconnected();                                  // 新进程从库里恢复
        service.flushBufferedForTest(10);
        CHECK(g_sent.size() == 2);
        CHECK(g_sent[0].second == "survive-restart");
        CHECK(g_sent[1].second == "survive-restart-2");
        CHECK(count(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0);
    }

    std::filesystem::remove_all(dir);
    std::cout << "mqtt_outbox_scenarios: " << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}