// =============================================================================
//  mqtt-cli  —— 远程服务器上的 MQTT 双向控制台
//
//  作用：让运维在一个日志文件里同时看到
//        "服务器发给盒子什么" 和 "盒子回给服务器什么"
//
//  两个子命令：
//    mqtt-cli watch              常驻订阅盒子上行主题，逐行追加到日志文件
//    mqtt-cli send <端点> <载荷>  向指定盒子下发命令，并把发送内容记进同一日志
//
//  端点取值：command / read / reboot     （对应盒子订阅的三个主题后缀）
//
//  依赖：libmosquitto-dev
//  编译：g++ -O2 -std=c++17 -Wall -Wextra -o mqtt-cli mqtt_cli.cpp -lmosquitto
//
//  与盒子代码的约定（见 docs/收包链路与协议对照.md）：
//    - 主题必须是 flexem/<boxNo>/<端点>，盒子只订阅这三个精确主题
//    - QoS 必须 0（盒子只实现了 QoS0，收到 QoS1 会把 packetId 当 payload）
//    - retain 必须 false（盒子 cleanSession=1 且不识别 RETAIN 位，
//      一旦 retain 会在每次重连时重放最后一条命令，导致误启动）
// =============================================================================

#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

// ---------------------------------------------------------------- 配置 ----

struct Config {
    std::string brokerHost = "127.0.0.1";
    int         brokerPort = 1883;
    std::string username;
    std::string password;
    std::string boxNo   = "123456";
    std::string logFile = "/root/program/MQTT/logs/mqtt.log";
    int         qos     = 0;        // 必须 0
    bool        retain  = false;    // 必须 false
};

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool loadConfig(const std::string& path, Config& c, std::string& err) {
    std::ifstream in(path);
    if (!in.is_open()) {
        err = "无法打开配置文件: " + path;
        return false;
    }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            err = "配置第 " + std::to_string(lineno) + " 行缺少 '=': " + t;
            return false;
        }
        const std::string k = trim(t.substr(0, eq));
        const std::string v = trim(t.substr(eq + 1));

        if      (k == "broker_host") c.brokerHost = v;
        else if (k == "broker_port") c.brokerPort = std::atoi(v.c_str());
        else if (k == "username")    c.username   = v;
        else if (k == "password")    c.password   = v;
        else if (k == "box_no")      c.boxNo      = v;
        else if (k == "log_file")    c.logFile    = v;
        else if (k == "qos")         c.qos        = std::atoi(v.c_str());
        else if (k == "retain")      c.retain     = (v == "true" || v == "1" || v == "yes");
        // 未知键静默忽略，方便以后加字段不被旧版本报错
    }

    if (c.brokerPort <= 0 || c.brokerPort > 65535) {
        err = "broker_port 非法";
        return false;
    }
    if (c.boxNo.empty()) {
        err = "box_no 不能为空";
        return false;
    }
    if (c.qos != 0) {
        std::cerr << "[警告] qos=" << c.qos << " 不被盒子支持，已强制改为 0" << std::endl;
        c.qos = 0;
    }
    if (c.retain) {
        std::cerr << "[警告] retain=true 会让盒子每次重连重放命令，已强制改为 false" << std::endl;
        c.retain = false;
    }
    return true;
}

// ---------------------------------------------------------------- 日志 ----

std::mutex    g_logMutex;
std::ofstream g_log;

bool openLog(const std::string& path, std::string& err) {
    g_log.open(path, std::ios::app);
    if (!g_log.is_open()) {
        err = "无法打开日志文件: " + path + "（目录是否存在？）";
        return false;
    }
    return true;
}

std::string nowStr() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

std::string padRight(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

// 一行一条，同时写文件 + 打 stdout（方便 systemd journal 里也能看）
void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_log.is_open()) {
        g_log << line << '\n';
        g_log.flush();     // 每行 flush：断电/崩溃最多丢当前一行
    }
    std::cout << line << std::endl;
}

// -------------------------------------------------------------- 主题工具 ----

std::string topicOf(const Config& c, const std::string& endpoint) {
    return "flexem/" + c.boxNo + "/" + endpoint;
}

// 取下主题最后一段：flexem/123456/command -> "command"
std::string endpointOf(const std::string& topic) {
    const size_t p = topic.rfind('/');
    return (p == std::string::npos) ? topic : topic.substr(p + 1);
}

// 服务器自己发出去的方向（command / read / reboot）。
// watch 收到这些主题时跳过，避免"自己发的又被自己记一遍"产生重复行。
bool isDownlink(const std::string& topic) {
    const std::string ep = endpointOf(topic);
    return ep == "command" || ep == "read" || ep == "reboot";
}

// -------------------------------------------------------------- 会话上下文 ----

struct Session {
    const Config*     cfg               = nullptr;
    std::string       expectReplyTopic;   // 非空 = send 模式，等这条主题的回执
    std::atomic<bool> connected{false};
    std::atomic<bool> gotReply{false};
    std::atomic<bool> finished{false};
    bool              announced       = false;   // watch 是否已打过"已连接"（每次重连再打一次）
};

volatile sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

// -------------------------------------------------------------- 回调 ----

void onConnect(struct mosquitto* mosq, void* obj, int rc) {
    auto* s = static_cast<Session*>(obj);
    if (rc != 0) {
        logLine(nowStr() + "  !!    连接失败 rc=" + std::to_string(rc) +
                " (" + mosquitto_strerror(rc) + ")");
        return;
    }

    if (s->expectReplyTopic.empty()) {
        // watch 模式：订阅所有盒子的所有上行主题
        const std::string sub = "flexem/+/#";
        mosquitto_subscribe(mosq, nullptr, sub.c_str(), 0);
        logLine(nowStr() + "  --    已连接 " + s->cfg->brokerHost + ":" +
                std::to_string(s->cfg->brokerPort) + "，订阅 " + sub);
    } else {
        // send 模式：订阅回执主题，等待 commandreply
        mosquitto_subscribe(mosq, nullptr, s->expectReplyTopic.c_str(), 0);
    }
    s->connected = true;
}

void onDisconnect(struct mosquitto* /*mosq*/, void* obj, int rc) {
    auto* s = static_cast<Session*>(obj);
    if (g_stop) return;   // 主动退出，不刷日志
    logLine(nowStr() + "  !!    连接断开 rc=" + std::to_string(rc) + "，2s 后自动重连");
    s->connected = false;
}

void onMessage(struct mosquitto* /*mosq*/, void* obj, const struct mosquitto_message* msg) {
    auto* s = static_cast<Session*>(obj);
    const std::string topic = msg->topic ? msg->topic : "";
    const std::string payload = msg->payload
        ? std::string(static_cast<const char*>(msg->payload), static_cast<size_t>(msg->payloadlen))
        : std::string();

    if (!s->expectReplyTopic.empty()) {
        if (topic == s->expectReplyTopic) {
            logLine(nowStr() + "  收←  " + padRight(topic, 42) + payload);
            s->gotReply = true;
            s->finished = true;
        }
        return;
    }

    if (isDownlink(topic)) return;   // send 已经记过"发→"，这里不再重复
    logLine(nowStr() + "  收←  " + padRight(topic, 42) + payload);
}

// -------------------------------------------------------------- 公共连接 ----

struct mosquitto* makeClient(const Config& cfg, Session& s, const std::string& clientId,
                             std::string& err) {
    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new(clientId.c_str(), true, &s);   // clean_session = true
    if (!mosq) {
        err = "mosquitto_new 失败";
        return nullptr;
    }

    mosquitto_connect_callback_set(mosq, onConnect);
    mosquitto_disconnect_callback_set(mosq, onDisconnect);
    mosquitto_message_callback_set(mosq, onMessage);

    if (!cfg.username.empty()) {
        mosquitto_username_pw_set(mosq, cfg.username.c_str(), cfg.password.c_str());
    }

    // 断线重连退避：2s 起，最长 30s，指数增长
    mosquitto_reconnect_delay_set(mosq, 2, 30, true);

    const int rc = mosquitto_connect(mosq, cfg.brokerHost.c_str(), cfg.brokerPort, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        err = std::string("连接 broker 失败: ") + mosquitto_strerror(rc);
        mosquitto_destroy(mosq);
        return nullptr;
    }
    return mosq;
}

void waitConnected(Session& s, int timeoutMs) {
    for (int i = 0; i < timeoutMs / 50 && !s.connected; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// -------------------------------------------------------------- watch ----

int cmdWatch(const Config& cfg) {
    Session s;
    s.cfg = &cfg;

    // clientId 必须和盒子（clientId = boxNo）不同，也不能和 send 撞，
    // 否则 MQTT 3.1.1 会把对方踢下线。
    const std::string clientId = "washcar-console";

    std::string err;
    struct mosquitto* mosq = makeClient(cfg, s, clientId, err);
    if (!mosq) {
        std::cerr << "[错误] " << err << std::endl;
        return 1;
    }

    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        std::cerr << "[错误] 启动网络线程失败" << std::endl;
        mosquitto_destroy(mosq);
        return 1;
    }

    logLine(nowStr() + "  --    mqtt-cli watch 启动，boxNo=" + cfg.boxNo +
            "，日志 " + cfg.logFile);

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    logLine(nowStr() + "  --    mqtt-cli watch 退出");
    return 0;
}

// -------------------------------------------------------------- send ----

int cmdSend(const Config& cfgIn, const std::string& endpoint, const std::string& payload,
            double waitSec) {
    if (endpoint != "command" && endpoint != "read" && endpoint != "reboot") {
        std::cerr << "[错误] 端点只能是 command / read / reboot，收到: " << endpoint << std::endl;
        return 2;
    }

    Config cfg = cfgIn;
    const std::string topic = topicOf(cfg, endpoint);

    Session s;
    s.cfg = &cfg;
    s.expectReplyTopic = topicOf(cfg, "commandreply");

    // 每次 send 用独立 clientId，避免和常驻 watch / 上一次 send 撞号互踢
    const std::string clientId = "washcar-send-" + std::to_string(getpid());

    std::string err;
    struct mosquitto* mosq = makeClient(cfg, s, clientId, err);
    if (!mosq) {
        std::cerr << "[错误] " << err << std::endl;
        return 1;
    }

    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        std::cerr << "[错误] 启动网络线程失败" << std::endl;
        mosquitto_destroy(mosq);
        return 1;
    }

    waitConnected(s, 5000);
    if (!s.connected) {
        std::cerr << "[错误] 5s 内未连上 broker " << cfg.brokerHost << ":" << cfg.brokerPort << std::endl;
        mosquitto_disconnect(mosq);
        mosquitto_loop_stop(mosq, false);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    // 给订阅留一点时间生效，否则可能错过很快的回执
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    int mid = 0;
    const int rc = mosquitto_publish(mosq, &mid, topic.c_str(),
                                     static_cast<int>(payload.size()),
                                     payload.data(), cfg.qos, cfg.retain ? 1 : 0);
    if (rc != MOSQ_ERR_SUCCESS) {
        logLine(nowStr() + "  !!    发送失败: " + mosquitto_strerror(rc) +
                "  主题 " + topic + "  载荷 " + payload);
        mosquitto_disconnect(mosq);
        mosquitto_loop_stop(mosq, false);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    logLine(nowStr() + "  发→  " + padRight(topic, 42) + payload);

    int exitCode = 0;
    if (waitSec > 0) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(static_cast<long>(waitSec * 1000));
        while (!s.finished && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!s.finished) {
            // 盒子失败分支不回 commandreply（源码里标了 B-NOT-YET-SENT），
            // 所以超时只说明"没有成功回执"，不等于命令没执行。
            logLine(nowStr() + "  !!    " + std::to_string(static_cast<int>(waitSec)) +
                    "s 内未收到 commandreply（盒子失败时本来就不回执，需读状态确认）");
            exitCode = 3;
        }
    }

    mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return exitCode;
}

// -------------------------------------------------------------- 帮助 ----

void usage() {
    std::cout <<
        "用法:\n"
        "  mqtt-cli watch [-c 配置文件]\n"
        "        常驻订阅 flexem/+/#，逐行写日志\n\n"
        "  mqtt-cli send <端点> <载荷> [--wait 秒] [-c 配置文件]\n"
        "        端点: command | read | reboot\n"
        "        --wait  发完等 commandreply 的秒数，默认 5，0 = 不等\n\n"
        "  mqtt-cli raw <完整主题> <载荷> [--wait 秒] [-c 配置文件]\n"
        "        兜底：发到任意主题（调试用）\n\n"
        "默认配置文件 /root/program/MQTT/config/mqtt.conf\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    const std::string cmd = argv[1];
    std::string configPath = "/root/program/MQTT/config/mqtt.conf";

    // 先扫 -c（位置无关）
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            configPath = argv[i + 1];
        }
    }

    Config cfg;
    std::string err;
    if (!loadConfig(configPath, cfg, err)) {
        std::cerr << "[错误] " << err << std::endl;
        return 2;
    }

    if (!openLog(cfg.logFile, err)) {
        std::cerr << "[错误] " << err << std::endl;
        return 2;
    }

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    if (cmd == "watch") {
        return cmdWatch(cfg);
    }

    if (cmd == "send" || cmd == "raw") {
        if (argc < 4) {
            usage();
            return 2;
        }
        const std::string target  = argv[2];
        const std::string payload = argv[3];

        double waitSec = 5.0;
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--wait") == 0 && i + 1 < argc) {
                waitSec = std::atof(argv[i + 1]);
            }
        }

        if (cmd == "raw") {
            // raw 只是把端点换成一个完整主题：临时改 boxNo 让 topicOf 拼出原样主题
            // 这里直接复用 send 的逻辑不方便，单独处理更直白。
            Config raw = cfg;
            Session s;
            s.cfg = &raw;
            s.expectReplyTopic = topicOf(raw, "commandreply");

            const std::string clientId = "washcar-send-" + std::to_string(getpid());
            struct mosquitto* mosq = makeClient(raw, s, clientId, err);
            if (!mosq) {
                std::cerr << "[错误] " << err << std::endl;
                return 1;
            }
            mosquitto_loop_start(mosq);
            waitConnected(s, 5000);
            if (!s.connected) {
                std::cerr << "[错误] 未连上 broker" << std::endl;
                mosquitto_destroy(mosq);
                mosquitto_lib_cleanup();
                return 1;
            }
            int mid = 0;
            mosquitto_publish(mosq, &mid, target.c_str(), static_cast<int>(payload.size()),
                              payload.data(), raw.qos, raw.retain ? 1 : 0);
            logLine(nowStr() + "  发→  " + padRight(target, 42) + payload);

            if (waitSec > 0) {
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(static_cast<long>(waitSec * 1000));
                while (!s.finished && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            mosquitto_disconnect(mosq);
            mosquitto_loop_stop(mosq, false);
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            return 0;
        }

        return cmdSend(cfg, target, payload, waitSec);
    }

    usage();
    return 2;
}
