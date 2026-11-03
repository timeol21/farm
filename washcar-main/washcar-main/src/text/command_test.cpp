// command_test.cpp — 命令服务自检。
// 覆盖三块:
//   1. 协议层: 端点 key、命令工厂、回执报文(golden)、主题拼装与反查
//   2. 命令服务: 主题 -> 命令对象 -> 大厅 -> 回执 的收发全链路
//   3. MQTT outbox: 离线入库、重连补发、只留最新、重启后补发
//
// 协议层不碰 MQTT、不碰 PLC —— 命令服务的"协议层"必须能被离线锁住,
// 这样改代码时一旦动了主题名或字段名, 这里立刻报。
// 收发链路用假通道 + 假大厅 + 假 DAO, 不碰 MQTT/PLC/SQLite, 秒级跑完。
//
// 用法: ./command_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "business_layer/command/command_service.h"
#include "business_layer/command/controller.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/mqtt/topic_resolver.h"
#include "business_layer/lobby/lobby_service.h"
#include "common/config/config_manager.h"
#include "common/sqlite/database_manager.h"
#include "presentation_layer/http_response.h"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using washcar::cfg::ConfigManager;

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

// 报文里出现了哪些 key(排序后拼接), 用来锁"字段集合"而不是顺序
std::string keySet(const std::string& payload) {
    const json message = json::parse(payload);
    std::set<std::string> keys;
    for (auto it = message.begin(); it != message.end(); ++it) keys.insert(it.key());
    std::string joined;
    for (const auto& key : keys) {
        if (!joined.empty()) joined += ",";
        joined += key;
    }
    return joined;
}

json commandBody(const std::string& field) {
    json body;
    body[field] = 1;
    Command::fillDefaults(body);
    return body;
}

// ---------- 命令服务用的假大厅 / 假 DAO ----------

// 只记录"被调了几次", 返回值可控
class FakeLobby : public ILobbyService {
public:
    int startCalls = 0;
    int stopCalls = 0;
    int readCalls = 0;
    bool startShouldFail = false;
    bool stopShouldFail = false;
    int failCode = static_cast<int>(ErrorCode::Code::WASHCAR_MAINTAINING);
    std::string failMessage = "maintaining";
    std::string lastBoxNo;

    WashCarOperationResult makeResult(ErrorCode::Code code, const std::string& message) {
        WashCarOperationResult result;
        result.setBoxNo(lastBoxNo);
        result.setCode(code);
        result.setMessage(message);
        return result;
    }

    std::vector<FxPlcStatus> viewPlcStatus() override { return {}; }
    LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery&) override {
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery&) override {
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery&) override {
        return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    bool isWashCarWashing() override { return false; }
    bool isMqttConnected() override { return true; }
    LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo&) override {
        return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation&) override {
        return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) override {
        ++startCalls;
        lastBoxNo = operation.getBoxNo();
        if (startShouldFail) {
            return LobbyResult<WashCarOperationResult>::Error(static_cast<ErrorCode::Code>(failCode), failMessage);
        }
        return LobbyResult<WashCarOperationResult>::Ok(makeResult(ErrorCode::Code::SUCCESS, "success"));
    }
    LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) override {
        ++stopCalls;
        lastBoxNo = operation.getBoxNo();
        if (stopShouldFail) {
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::WASHCAR_STOP_FAILED,
                                                              "stop failed");
        }
        return LobbyResult<WashCarOperationResult>::Ok(makeResult(ErrorCode::Code::SUCCESS, "stopped"));
    }
    LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) override {
        lastBoxNo = operation.getBoxNo();
        return LobbyResult<WashCarOperationResult>::Ok(makeResult(ErrorCode::Code::SUCCESS, "success"));
    }
    LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation& operation) override {
        ++readCalls;
        lastBoxNo = operation.getBoxNo();
        WashCarOperationResult result = makeResult(ErrorCode::Code::SUCCESS, "read ok");
        result.addStatusPoint("D142", 3);
        return LobbyResult<WashCarOperationResult>::Ok(result);
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
    LobbyResult<bool> changeMode(const WashCarOperation&) override { return LobbyResult<bool>::Ok(true); }
    LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation&) override {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
    LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation&) override {
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

class FakeDao : public ICommandDao {
public:
    int inserts = 0;
    Command::Type lastType = Command::Type::OTHER;

    bool insertCommand(const CommandEntity& cmd) override {
        ++inserts;
        lastType = cmd.type;
        return true;
    }
};

std::string topicOf(Command::Endpoint endpoint) {
    const auto& config = ConfigManager::instance();
    return TopicResolver::build(config.mqtt().topics, config.box().boxNo, Command::keyOf(endpoint));
}

json parsePayload(const Command::Message& message) {
    return json::parse(message.payload);
}

// ---------- outbox 用的替身 ----------

// controller 在 outbox 测试里用不到(我们从不路由收到的消息),给个空壳即可。
// 这样不用把整个大厅服务链进测试。
class NullController : public IController {
public:
    HttpResponse handleHttp(const std::string&, const std::string&) override { return HttpResponse(); }
    void handleMqtt(const std::string&, const std::string&) override {}
};

// 发送记录:topic 后半段 -> 收到的 payload 顺序
std::vector<std::pair<std::string, std::string>> g_sent;
bool g_sendShouldFail = false;

int dbCount(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    int n = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK
        && sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

// 从库里按 id 升序读出所有未发消息的 payload, 用来验证顺序和条数
std::vector<std::string> unsentPayloads(sqlite3* db) {
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

void resetOutbox(sqlite3* db) {
    sqlite3_exec(db, "DELETE FROM mqtt_messages;", nullptr, nullptr, nullptr);
    MessageBuffer::getInstance().clear();
    g_sent.clear();
    g_sendShouldFail = false;
}

}  // namespace

int main() {
    ConfigManager::instance().applyDefaults();
    const std::string boxNo = ConfigManager::instance().box().boxNo;

    // =====================================================================
    // 1. 协议层: 端点 / 命令工厂 / 回执 / 主题
    // =====================================================================

    // --- 1.1 端点与配置 key 一一对应 ---
    {
        const Command::Endpoint endpoints[] = {
            Command::Endpoint::COMMAND, Command::Endpoint::READ, Command::Endpoint::REBOOT,
            Command::Endpoint::COMMANDREPLY, Command::Endpoint::READDATA,
            Command::Endpoint::MONITOR, Command::Endpoint::PUSHALARM
        };
        for (Command::Endpoint endpoint : endpoints) {
            Command::Endpoint parsed;
            const std::string key = Command::keyOf(endpoint);
            expect(!key.empty(), "1.1 端点有 key: " + std::to_string(static_cast<int>(endpoint)));
            expect(Command::fromKey(key, parsed) && parsed == endpoint, "1.1 key 能反查回端点: " + key);
            expect(!TopicResolver::build(ConfigManager::instance().mqtt().topics, boxNo, key).empty(),
                   "1.1 主题能拼出来: " + key);
        }
        Command::Endpoint unknown;
        expect(!Command::fromKey("no_such_endpoint", unknown), "1.1 未知 key 不映射");
    }

    // --- 1.2 下行命令工厂: 字段名来自点位表, 不写死机型 ---
    {
        auto command = Command::create(Command::Endpoint::COMMAND, commandBody("Manual start"));
        expect(command && command->type() == Command::Type::WASHCAR_START, "1.2 Manual start -> 启动命令");
        expect(command->endpoint() == Command::Endpoint::COMMAND, "1.2 启动命令的入口端点是 command");
        expect(std::string(Command::keyOf(command->replyEndpoint())) == "commandreply", "1.2 启动命令回 commandreply");
    }
    {
        auto command = Command::create(Command::Endpoint::COMMAND, commandBody("stop"));
        expect(command && command->type() == Command::Type::WASHCAR_STOP, "1.2 stop -> 停止命令");
    }
    {
        auto command = Command::create(Command::Endpoint::COMMAND, commandBody("reset"));
        expect(command && command->type() == Command::Type::WASHCAR_RESET, "1.2 reset -> 复位命令");
    }
    {
        json body;
        Command::fillDefaults(body);
        expect(Command::create(Command::Endpoint::COMMAND, body) == nullptr, "1.2 无命令字段 -> 不造命令");
        body["unknown field"] = 1;
        expect(Command::create(Command::Endpoint::COMMAND, body) == nullptr, "1.2 不认识的字段 -> 不造命令");
    }
    {
        json body;
        Command::fillDefaults(body);
        auto command = Command::create(Command::Endpoint::READ, body);
        expect(command && command->type() == Command::Type::WASHCAR_READ, "1.2 read 端点 -> 读命令");
        expect(std::string(Command::keyOf(command->replyEndpoint())) == "readdata", "1.2 读命令回 readdata");
    }
    {
        auto command = Command::create(Command::Endpoint::REBOOT, commandBody("reboot"));
        expect(command && command->type() == Command::Type::REBOOT, "1.2 reboot 端点 -> 重启命令");
        expect(std::string(Command::keyOf(command->replyEndpoint())) == "commandreply", "1.2 重启回 commandreply");
    }

    // --- 1.3 公共字段补齐 ---
    {
        json body;
        body["Manual start"] = 1;
        Command::fillDefaults(body);
        expect(body.value("boxNo", "") == boxNo, "1.3 boxNo 缺省补配置值");
        expect(!body.value("plcId", "").empty(), "1.3 plcId 缺省补默认值");
        expect(body.value("reqSource", "") == "mqtt", "1.3 reqSource 固定 mqtt");

        body["boxNo"] = "999888777666";
        body["plcId"] = "fx_custom";
        Command::fillDefaults(body);
        expect(body["boxNo"] == "999888777666", "1.3 云端给了 boxNo 不覆盖");
        expect(body["plcId"] == "fx_custom", "1.3 云端给了 plcId 不覆盖");
    }

    // --- 1.4 回执报文(golden) ---
    {
        auto command = Command::create(Command::Endpoint::COMMAND, commandBody("Manual start"));
        json body = commandBody("Manual start");
        expect(command->parse(body), "1.4 启动命令报文可解析");

        Command::Result success;
        success.ok = true;
        success.code = 0;
        success.message = "success";
        success.boxNo = boxNo;
        Command::Message reply = command->reply(success);
        expect(reply.topicKey == "commandreply", "1.4 成功回执走 commandreply");
        expect(reply.type == MessageBuffer::MessageType::TYPE_WASHCAR_RESULT, "1.4 回执缓冲类型沿用旧值");
        expect(keySet(reply.payload) == "message,time", "1.4 成功回执字段 = message,time");
        expect(json::parse(reply.payload)["message"] == "success", "1.4 成功回执 message=success");

        Command::Result failure;
        failure.ok = false;
        failure.code = static_cast<int>(ErrorCode::Code::WASHCAR_MAINTAINING);
        failure.message = "maintaining";
        Command::Message failReply = command->reply(failure);
        expect(keySet(failReply.payload) == "code,message,time", "1.4 失败回执字段 = code,message,time");
        expect(json::parse(failReply.payload)["message"] == "maintaining", "1.4 失败回执透传业务文案");
        expect(json::parse(failReply.payload)["code"] == static_cast<int>(ErrorCode::Code::WASHCAR_MAINTAINING),
               "1.4 失败回执透传错误码");
    }
    {
        // read 回执: 点位键大写 + boxNo + time, 失败再补 message/code
        json body;
        Command::fillDefaults(body);
        auto command = Command::create(Command::Endpoint::READ, body);
        expect(command->parse(body), "1.4 读命令报文可解析");

        Command::Result success;
        success.ok = true;
        success.boxNo = boxNo;
        success.points["D142"] = 3;
        success.points["y74"] = 1;
        Command::Message reply = command->reply(success);
        expect(reply.topicKey == "readdata", "1.4 读回执走 readdata");
        const json message = json::parse(reply.payload);
        expect(message.contains("D142") && message["D142"] == 3, "1.4 读回执带 D142");
        expect(message.contains("Y74") && message["Y74"] == 1, "1.4 读回执点位键转大写");
        expect(message.contains("boxNo") && message["boxNo"] == boxNo, "1.4 读回执带 boxNo");
        expect(message.contains("time"), "1.4 读回执带 time");
        expect(!message.contains("code"), "1.4 成功读回执不带 code");

        Command::Result failure;
        failure.ok = false;
        failure.code = static_cast<int>(ErrorCode::Code::WASHCAR_NO_PLC);
        failure.message = "no plc";
        failure.boxNo = boxNo;
        const json failMessage = json::parse(command->reply(failure).payload);
        expect(failMessage.contains("code") && failMessage.contains("message"), "1.4 失败读回执补 code/message");
    }
    {
        // reboot 报文非法: 回执走业务化文案
        auto command = Command::create(Command::Endpoint::REBOOT, json::object());
        json body = json::object();
        expect(command->parse(body), "1.4 reboot 报文解析不抛异常");

        Command::Result failed;
        failed.ok = false;
        failed.code = 400;
        failed.message = "invalid reboot payload";
        const json message = json::parse(command->reply(failed).payload);
        expect(message["message"] == "invalid reboot payload", "1.4 非法 reboot 回执文案不变");
        expect(message["code"] == 400, "1.4 非法 reboot 回执 code=400");
    }

    // --- 1.5 主题解析: 默认/点号/自定义路径与反查 ---
    {
        washcar::cfg::MqttTopicsConfig def;
        expect(TopicResolver::build(def, "300825", "monitor") == "flexem/300825/monitor",
               "1.5 默认斜杠主题");
        expect(TopicResolver::build(def, "300825", "command") == "flexem/300825/command",
               "1.5 默认斜杠 command");

        washcar::cfg::MqttTopicsConfig dot;
        dot.monitor = "flexem.{boxNo}.monitor";
        dot.read = "flexem.{boxNo}.read";
        dot.reboot = "flexem.{boxNo}.reboot";
        expect(TopicResolver::build(dot, "abc", "monitor") == "flexem.abc.monitor", "1.5 点号主题");

        std::string key;
        expect(TopicResolver::resolve(dot, "abc", "flexem.abc.read", key) && key == "read",
               "1.5 点号主题反查");
        expect(!TopicResolver::resolve(dot, "abc", "flexem.abc.unknown", key), "1.5 未知主题被拒");

        washcar::cfg::MqttTopicsConfig custom;
        custom.command = "factory/line1/{boxNo}/cmd";
        expect(TopicResolver::build(custom, "box1", "command") == "factory/line1/box1/cmd",
               "1.5 自定义多段主题");
        expect(TopicResolver::resolve(custom, "box1", "factory/line1/box1/cmd", key) && key == "command",
               "1.5 自定义主题反查");
        expect(TopicResolver::all(def, "x").size() == 7, "1.5 all() 返回 7 条");
    }

    // =====================================================================
    // 2. 命令服务收发全链路
    // =====================================================================
    {
        FakeDao dao;
        CommandService service(dao);
        std::vector<Command::Message> channel;
        service.setSenderForTest([&channel](const Command::Message& message) {
            channel.push_back(message);
        });
        FakeLobby lobby;

        // --- 2.1 启动命令: 成功回执 ---
        {
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"Manual start":1})");
            expect(lobby.startCalls == 1, "2.1 启动命令到达大厅");
            expect(channel.size() == 1, "2.1 启动命令只发一条回执");
            expect(channel[0].topicKey == "commandreply", "2.1 启动回执走 commandreply");
            expect(parsePayload(channel[0])["message"] == "success", "2.1 启动回执 message=success");
        }

        // --- 2.2 启动命令: 业务失败也回执, 并透传文案与错码 ---
        {
            lobby.startShouldFail = true;
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"Manual start":1})");
            expect(channel.size() == 1, "2.2 失败也回一条");
            const json message = parsePayload(channel[0]);
            expect(message["message"] == "maintaining", "2.2 失败回执透传业务文案");
            expect(message["code"] == static_cast<int>(ErrorCode::Code::WASHCAR_MAINTAINING),
                   "2.2 失败回执透传错码");
            lobby.startShouldFail = false;
        }

        // --- 2.3 停止命令: 成功只有 SUCCESS 一个码 ---
        {
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"stop":1})");
            expect(lobby.stopCalls == 1, "2.3 停止命令到达大厅");
            expect(channel.size() == 1, "2.3 停止回执一条");
            expect(parsePayload(channel[0])["message"] == "success", "2.3 停止成功回 message=success");

            // 停止失败(PLC 写失败/确认超时)必须回失败回执, 不能被当成成功
            lobby.stopShouldFail = true;
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"stop":1})");
            expect(channel.size() == 1, "2.3 停止失败也回一条");
            const json failed = parsePayload(channel[0]);
            expect(failed["message"] == "stop failed", "2.3 停止失败透传业务文案");
            expect(failed["code"] == static_cast<int>(ErrorCode::Code::WASHCAR_STOP_FAILED),
                   "2.3 停止失败透传错码");
            lobby.stopShouldFail = false;
        }

        // --- 2.4 read: 回 readdata, 带点值与 boxNo ---
        {
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::READ), R"({"D142":null})");
            expect(lobby.readCalls == 1, "2.4 read 命令到达大厅");
            expect(channel.size() == 1, "2.4 read 回执一条");
            expect(channel[0].topicKey == "readdata", "2.4 read 回 readdata");
            const json message = parsePayload(channel[0]);
            expect(message["D142"] == 3, "2.4 read 回执带点值");
            expect(message["boxNo"] == boxNo, "2.4 read 回执带 boxNo");
            expect(channel[0].type == MessageBuffer::MessageType::TYPE_WASHCAR_RESULT,
                   "2.4 read 缓冲类型沿用旧值");
        }

        // --- 2.5 不认识的命令 / 坏 JSON: 回失败回执 ---
        {
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"whatever":1})");
            expect(channel.size() == 1, "2.5 未知命令回一条");
            const json message = parsePayload(channel[0]);
            expect(message["message"] == "unsupported command", "2.5 未知命令文案不变");
            expect(message["code"] == 400, "2.5 未知命令 code=400");
        }
        {
            channel.clear();
            service.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"Manual start":)");
            expect(channel.size() == 1, "2.5 坏 JSON 回一条");
            const json message = parsePayload(channel[0]);
            expect(message["message"] == "invalid json payload", "2.5 坏 JSON 文案不变");
            expect(message["code"] == 400, "2.5 坏 JSON code=400");
        }

        // --- 2.6 非本机主题 / 上行端点: 静默丢弃 ---
        {
            channel.clear();
            service.receive(lobby, "flexem/other-box/command", R"({"Manual start":1})");
            expect(channel.empty(), "2.6 别的盒号主题不处理");
            service.receive(lobby, topicOf(Command::Endpoint::MONITOR), R"({"D142":1})");
            expect(channel.empty(), "2.6 上行主题不当命令处理");
        }

        // --- 2.7 订阅主题: 3 个下行端点, 全部来自配置 ---
        {
            const std::vector<std::string> topics = service.subscriptionTopics();
            expect(topics.size() == 3, "2.7 订阅 3 个主题");
            expect(topics.size() == 3 && topics[0] == topicOf(Command::Endpoint::COMMAND), "2.7 第 1 个是 command");
            expect(topics.size() == 3 && topics[1] == topicOf(Command::Endpoint::READ), "2.7 第 2 个是 read");
            expect(topics.size() == 3 && topics[2] == topicOf(Command::Endpoint::REBOOT), "2.7 第 3 个是 reboot");
        }

        // --- 2.8 命令台账: 大厅执行到业务层后记一条 ---
        {
            const int before = dao.inserts;
            service.recordCommand(Command::Type::WASHCAR_START, boxNo);
            expect(dao.inserts == before + 1, "2.8 recordCommand 落库一次");
            expect(dao.lastType == Command::Type::WASHCAR_START, "2.8 落库类型是启动");
        }

        // --- 2.9 通道未注入: 不发但也不崩 ---
        {
            CommandService detached(dao);
            detached.receive(lobby, topicOf(Command::Endpoint::COMMAND), R"({"Manual start":1})");
            expect(true, "2.9 通道未注入时收发不崩");
        }
    }

    // =====================================================================
    // 3. MQTT outbox: 离线补发
    // 替身注入点: MqttService::setSendOverrideForTest(发送成功/失败可控)
    //            MqttService::setConnectedForTest(模拟断网/恢复)
    //            MqttService::flushBufferedForTest(手动触发补发)
    // 三个入口生产代码都不调用, 默认走真实 socket, 行为不变。
    // =====================================================================
    {
        const auto dir = std::filesystem::temp_directory_path() / "washcar_outbox_scenarios";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        DatabaseManager& db = DatabaseManager::instance();
        if (!db.init(dir.string())) {
            std::cout << "  [FAIL] 数据库初始化失败" << std::endl;
            return 2;
        }
        sqlite3* raw = db.getCommandDB();
        auto& buffer = MessageBuffer::getInstance();
        buffer.clear();
        buffer.setDatabase(raw);
        buffer.setLogger([](const std::string&) {});   // 静音, 测试输出更干净

        NullController controller;
        MqttProtocol protocol;
        MqttService service("127.0.0.1", 1883, "u", "p", "testbox", controller, protocol);
        service.setSendOverrideForTest([](const std::string& topic, const std::string& payload) {
            if (g_sendShouldFail) return false;
            g_sent.emplace_back(topic, payload);
            return true;
        });

        using MsgType = MessageBuffer::MessageType;
        const MsgType FAULT = MessageBuffer::MessageType::TYPE_FAULT_REPORT;
        const MsgType STATUS = MessageBuffer::MessageType::TYPE_WASHCAR_STATUS;
        // 命令结果类不参与"只留最新",每条都保留
        const MsgType RESULT = MessageBuffer::MessageType::TYPE_POINT_RESULT;

        // --- 3.1 在线直发, 库里不留未发记录 ---
        {
            resetOutbox(raw);
            service.setConnectedForTest(true);
            service.publishWithBuffer("event/post", "{\"fault\":1}", FAULT);
            expect(g_sent.size() == 1, "3.1 确实发出去了");
            expect(g_sent[0].second == "{\"fault\":1}", "3.1 发出去的 payload 一致");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0, "3.1 库里没有积压");
        }

        // --- 3.2 发送失败后入库, 下轮补发并按 id 标记 ---
        {
            resetOutbox(raw);
            service.setConnectedForTest(true);
            g_sendShouldFail = true;                                     // 模拟 socket 写失败
            service.publishWithBuffer("event/post", "{\"fault\":2}", FAULT);
            g_sendShouldFail = false;
            expect(g_sent.empty(), "3.2 没发出去");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 1, "3.2 进库了");

            service.handleReconnected();                                  // 模拟重连: 把库里的装进内存队列
            service.flushBufferedForTest(10);                             // 补发
            expect(g_sent.size() == 1, "3.2 补发一条");
            expect(g_sent[0].second == "{\"fault\":2}", "3.2 补发的 payload 一致");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0, "3.2 补发后标记完成");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=1") == 1, "3.2 已发送计数为 1");
        }

        // --- 3.3 离线 3 条故障全部入库, 重连后只补发最新一条 ---
        {
            resetOutbox(raw);
            service.setConnectedForTest(false);                          // 断网
            service.publishWithBuffer("event/post", "fault-A", FAULT);
            service.publishWithBuffer("event/post", "fault-B", FAULT);
            service.publishWithBuffer("event/post", "fault-C", FAULT);
            expect(g_sent.empty(), "3.3 断网期间一条都没发出去");
            // 入库时不合并: 断网期间发生了什么就如实存了什么
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 3, "3.3 三条都进库");

            service.setConnectedForTest(true);
            service.handleReconnected();
            // 重连时收敛: 旧的直接删除, 只留最新一条待补发
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages") == 1, "3.3 重连后只留一条");

            service.flushBufferedForTest(10);
            expect(g_sent.size() == 1, "3.3 只补发一条");
            expect(g_sent[0].second == "fault-C", "3.3 补发的是最新那条");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0, "3.3 补发后没有未发记录");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=1") == 1, "3.3 已发送计数为 1");
        }

        // --- 3.4 状态类离线期间变化 5 次, 重连后只发最新一条 ---
        {
            resetOutbox(raw);
            service.setConnectedForTest(false);
            for (int i = 1; i <= 5; ++i) {
                service.publishWithBuffer("monitor", "{\"step\":" + std::to_string(i) + "}", STATUS);
            }
            const auto kept = unsentPayloads(raw);
            expect(kept.size() == 1, "3.4 库里只留最新");
            expect(kept[0] == "{\"step\":5}", "3.4 留的是第 5 次的值");

            service.setConnectedForTest(true);
            service.handleReconnected();
            service.flushBufferedForTest(10);
            expect(g_sent.size() == 1, "3.4 只发一条");
            expect(g_sent[0].second == "{\"step\":5}", "3.4 发的是最新值");
        }

        // --- 3.5 重启进程后, 库里未发消息仍能补发 ---
        {
            resetOutbox(raw);
            service.setConnectedForTest(false);
            service.publishWithBuffer("event/post", "survive-restart", RESULT);
            service.publishWithBuffer("event/post", "survive-restart-2", RESULT);
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 2, "3.5 两条都进库");

            // 模拟进程重启: 内存队列全丢(handleConnectionLost 把内存里的存回库后 buffer 空),
            // 只剩数据库这一个事实来源。
            service.handleConnectionLost();
            MessageBuffer::getInstance().clear();

            service.setConnectedForTest(true);
            service.handleReconnected();                                  // 新进程从库里恢复
            service.flushBufferedForTest(10);
            expect(g_sent.size() == 2, "3.5 两条都补发");
            expect(g_sent[0].second == "survive-restart", "3.5 补发顺序保持");
            expect(g_sent[1].second == "survive-restart-2", "3.5 补发顺序保持");
            expect(dbCount(raw, "SELECT count(*) FROM mqtt_messages WHERE sent=0") == 0, "3.5 补发后没有未发记录");
        }

        std::filesystem::remove_all(dir);
    }

    std::cout << "command_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
