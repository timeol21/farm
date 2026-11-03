#include "presentation_layer/admin_controller.h"

#include "business_layer/lobby/lobby_service.h"
#include "business_layer/device/device_service.h"
#include "business_layer/safety/auth_service.h"
#include "business_layer/command/mqtt/topic_resolver.h"
#include "common/config/config_manager.h"
#include "common/config/config_serialization.h"
#include "common/config/default_point.h"
#include "common/log/log_manager.h"
#include "common/log/log_query.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;

namespace {

HttpResponse jsonResponse(int status, const json& body, bool allowCors = false,
                          const std::vector<std::string>& headers = {}) {
    HttpResponse response;
    response.status = status;
    response.body = body.dump();
    response.contentType = "application/json; charset=utf-8";
    response.allowCors = allowCors;
    response.headers = headers;
    return response;
}

HttpResponse errorResponse(int status, const std::string& message) {
    return jsonResponse(status, json{{"ok", false}, {"error", message}});
}

bool parseBody(const HttpRequest& request, json& body, std::string& error) {
    try {
        body = request.body.empty() ? json::object() : json::parse(request.body);
        return true;
    } catch (const std::exception& e) {
        error = std::string("JSON 解析失败: ") + e.what();
        return false;
    }
}

json topicsJson(const washcar::cfg::MqttTopicsConfig& topics, const std::string& boxNo) {
    json result = json::array();
    for (const auto& [key, topic] : TopicResolver::all(topics, boxNo)) {
        result.push_back(json{{"key", key}, {"topic", topic}});
    }
    return result;
}

// 取字符串字段:字段不存在用 fallback,类型不对返回 false 让上层回 400。
// 不能用 json::value() —— 它内部直接 get<std::string>(),类型不符会抛
// type_error。异常一旦逃到 HTTP 工作线程外就是 std::terminate,整个盒子
// 进程 abort:PLC 采集、云端链路、正在进行的洗车全断,一台机器被一条
// 畸形请求打挂。
bool readString(const json& body, const char* key, std::string& out,
                const std::string& fallback = "") {
    const auto it = body.find(key);
    if (it == body.end()) {
        out = fallback;
        return true;
    }
    if (!it->is_string()) return false;
    try {
        out = it->get<std::string>();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// 取整数字段:同上,还要挡超出 int 范围的值(超大数字 get<int>() 抛 out_of_range)。
bool readInt(const json& body, const char* key, int& out, int fallback) {
    const auto it = body.find(key);
    if (it == body.end()) {
        out = fallback;
        return true;
    }
    if (!it->is_number_integer() && !it->is_number_unsigned()) return false;
    try {
        out = it->get<int>();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

}  // namespace

AdminController::AdminController(ILobbyService& lobby, IDeviceService& device, AuthService& auth,
                                     const std::string& logDir)
    : m_lobby(lobby), m_device(device), m_auth(auth), m_logDir(logDir) {}

HttpResponse AdminController::handleHttp(const std::string&, const std::string&) {
    return errorResponse(404, "Not found");
}

void AdminController::handleMqtt(const std::string&, const std::string&) {}

HttpResponse AdminController::handle(const HttpRequest& request) {
    try {
        return route(request);
    } catch (const json::exception& e) {
        LOG_ERROR(std::string("[ConfigUI] bad request: ") + e.what());
        return errorResponse(400, "请求参数格式错误");
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("[ConfigUI] request failed: ") + e.what());
        return errorResponse(500, "请求处理失败");
    } catch (...) {
        LOG_ERROR("[ConfigUI] request failed: unknown error");
        return errorResponse(500, "请求处理失败");
    }
}

HttpResponse AdminController::route(const HttpRequest& request) {
    if (request.method != "POST") return errorResponse(405, "Method Not Allowed");
    if (request.path == "/api/login") return handleLogin(request);
    if (request.path == "/api/logout") return handleLogout(request);
    if (request.path == "/api/session") return handleSession(request);
    if (request.path == "/api/status") return handleStatus(request);
    if (request.path == "/api/config") return handleConfig(request);
    if (request.path == "/api/config/validate") return handleValidate(request);
    if (request.path == "/api/config/apply") return handleApply(request);
    if (request.path == "/api/config/history") return handleHistory(request);
    if (request.path == "/api/config/rollback") return handleRollback(request);
    if (request.path == "/api/plc/test-read") return handleTestRead(request);
    if (request.path == "/api/password") return handlePassword(request);
    if (request.path == "/api/system/restart") return handleRestart(request);
    if (request.path == "/api/logs") return handleLogs(request);
    return errorResponse(404, "Not found");
}

bool AdminController::requireLogin(const HttpRequest& request, std::string& username,
                                    HttpResponse& denied) const {
    const std::string token = AuthService::extractToken(request.cookie);
    if (m_auth.validate(token, username)) return true;
    denied = errorResponse(401, "请先登录");
    return false;
}

HttpResponse AdminController::handleLogin(const HttpRequest& request) {
    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    std::string username;
    std::string password;
    if (!readString(body, "username", username) || !readString(body, "password", password)) {
        return errorResponse(400, "username / password 必须是字符串");
    }
    // 还没改过密码时允许密码留空,等价于默认 washcar;改过密码后留空仍然失败。
    if (password.empty() && m_auth.usingDefaultPassword()) password = "washcar";

    std::string token;
    std::string error;
    if (!m_auth.login(username, password, token, error)) {
        LOG_WARNING("[ConfigUI] login failed, user=" + username + ", ip=" + request.clientIp);
        return errorResponse(401, error);
    }
    LOG_INFO("[ConfigUI] login success, user=" + username + ", ip=" + request.clientIp);
    return jsonResponse(200, json{{"ok", true}, {"username", username}},
                        false, {AuthService::sessionCookie(token)});
}

HttpResponse AdminController::handleLogout(const HttpRequest& request) {
    m_auth.logout(AuthService::extractToken(request.cookie));
    return jsonResponse(200, json{{"ok", true}}, false, {AuthService::sessionCookie("", true)});
}

HttpResponse AdminController::handleSession(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) {
        return jsonResponse(200, json{{"authenticated", false},
                                      {"defaultPassword", m_auth.usingDefaultPassword()}});
    }
    return jsonResponse(200, json{{"authenticated", true},
                                  {"username", username},
                                  {"defaultPassword", m_auth.usingDefaultPassword()}});
}

HttpResponse AdminController::handleStatus(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    const auto& cfg = ConfigManager::instance();
    json out;
    out["ok"] = true;
    out["boxNo"] = cfg.box().boxNo;
    out["boxType"] = cfg.box().boxType;
    out["revision"] = cfg.revision();
    out["degraded"] = cfg.degraded();
    out["configPath"] = cfg.path();
    out["topics"] = topicsJson(cfg.mqtt().topics, cfg.box().boxNo);
    out["mqtt"] = json{{"brokerIp", cfg.mqtt().brokerIp},
                       {"port", cfg.mqtt().port},
                       {"connected", m_lobby.isMqttConnected()}};

    json plc;
    plc["connected"] = false;
    plc["points"] = json::object();
    const std::vector<FxPlcStatus> fxList = m_lobby.viewPlcStatus();
    if (!fxList.empty()) {
        plc["connected"] = true;
        const auto& fx = fxList.front();
        for (const auto& [addr, value] : fx.getDMap()) plc["points"]["D" + std::to_string(addr)] = value;
        for (const auto& [addr, value] : fx.getMMap()) plc["points"]["M" + std::to_string(addr)] = value ? 1 : 0;
        for (const auto& [addr, value] : fx.getXMap()) plc["points"]["X" + std::to_string(addr)] = value ? 1 : 0;
        for (const auto& [addr, value] : fx.getYMap()) plc["points"]["Y" + std::to_string(addr)] = value ? 1 : 0;
        for (const auto& [addr, value] : fx.getSMap()) plc["points"]["S" + std::to_string(addr)] = value ? 1 : 0;
    }
    out["plc"] = plc;

    const std::string plcId = cfg.box().boxType == "yiGongLi" ? cfg.plcTcp().plcId : "fx_plc_01";
    json opJson = {{"boxNo", cfg.box().boxNo}, {"plcId", plcId}, {"reqSource", "http"}};
    WashCarOperation operation(opJson);
    operation.setProductId("config");
    operation.setDeviceNum(cfg.box().boxNo);
    auto washResult = m_lobby.getWashCarStatus(operation);
    json wash;
    wash["washing"] = m_lobby.isWashCarWashing();
    if (washResult.data.has_value()) {
        wash["code"] = static_cast<int>(washResult.data->getCode());
        wash["message"] = washResult.data->getMessage();
        wash["currentStep"] = washResult.data->getCurrentStep();
        wash["faultCode"] = washResult.data->getFaultCode();
        json points = json::object();
        for (const auto& [key, value] : washResult.data->getStatus()) points[key] = value;
        wash["points"] = points;
    } else {
        wash["code"] = static_cast<int>(washResult.errorCode);
        wash["message"] = washResult.message;
    }
    out["washcar"] = wash;
    out["serverTime"] = static_cast<long long>(std::time(nullptr));
    return jsonResponse(200, out);
}

HttpResponse AdminController::handleConfig(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    const auto& cfg = ConfigManager::instance();
    json raw = cfg.raw();
    bool passwordSet = false;
    if (raw.contains("mqtt") && raw["mqtt"].is_object()) {
        passwordSet = raw["mqtt"].contains("password") &&
                      raw["mqtt"]["password"].is_string() &&
                      !raw["mqtt"]["password"].get<std::string>().empty();
        raw["mqtt"]["password"] = "";
        raw["mqtt"]["passwordSet"] = passwordSet;
    }
    if (raw.contains("admin") && raw["admin"].is_object()) {
        raw["admin"].erase("passwordHash");
    }

    json out;
    out["ok"] = true;
    out["revision"] = cfg.revision();
    out["degraded"] = cfg.degraded();
    out["configPath"] = cfg.path();
    out["raw"] = raw;
    out["mqtt"] = raw.contains("mqtt") ? raw["mqtt"] : json::object();
    out["box"] = raw.contains("box") ? raw["box"] : json::object();
    // 老配置可能没写 boxType;前端要拿到当前生效值。
    out["box"]["boxNo"] = cfg.box().boxNo;
    out["box"]["boxType"] = cfg.box().boxType;
    out["topics"] = topicsJson(cfg.mqtt().topics, cfg.box().boxNo);
    out["effectiveMachine"] = json(cfg.machine());
    out["defaultPassword"] = m_auth.usingDefaultPassword();

    const auto& base = default_point::getPointTable(cfg.box().boxType);
    json defaultNames = json::array();
    for (const auto& signal : base.signals) defaultNames.push_back(signal.name);
    out["defaultSignalNames"] = defaultNames;

    json custom = json::array();
    if (raw.contains("machine") && raw["machine"].is_object() &&
        raw["machine"].contains(cfg.box().boxType) &&
        raw["machine"][cfg.box().boxType].is_object() &&
        raw["machine"][cfg.box().boxType].contains("signals")) {
        // 老配置可能写的是完整表:只把非默认信号交给前端当"自定义点",
        // 否则前端会把系统点位也当成可编辑项。
        for (const auto& signal : raw["machine"][cfg.box().boxType]["signals"]) {
            if (!signal.is_object()) continue;
            std::string name;
            if (!readString(signal, "name", name)) continue;
            bool isDefault = false;
            for (const auto& def : defaultNames) if (def.get<std::string>() == name) { isDefault = true; break; }
            if (!isDefault) custom.push_back(signal);
        }
    } else {
        // 老配置可能写的是完整表;把非默认信号挑出来当作自定义点
        for (const auto& signal : cfg.machine().signals) {
            bool isDefault = false;
            for (const auto& name : defaultNames) if (name == signal.name) { isDefault = true; break; }
            if (!isDefault) custom.push_back(json(signal));
        }
    }
    out["customSignals"] = custom;
    return jsonResponse(200, out);
}

HttpResponse AdminController::handleValidate(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    const json patch = body.contains("patch") ? body["patch"] : body;
    std::string expected;
    if (!readString(body, "expectedRevision", expected)) {
        return errorResponse(400, "expectedRevision 必须是字符串");
    }

    json merged;
    std::string error;
    const bool valid = ConfigManager::instance().validatePatch(patch, expected, merged, error);
    json out{{"ok", valid}, {"valid", valid}};
    if (valid) {
        out["topics"] = merged.contains("mqtt") && merged["mqtt"].contains("topics")
            ? merged["mqtt"]["topics"] : json::object();
    } else {
        out["error"] = error;
    }
    return jsonResponse(valid ? 200 : 422, out);
}

HttpResponse AdminController::handleApply(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    if (m_lobby.isWashCarWashing()) {
        return jsonResponse(409, json{{"ok", false}, {"code", "washing"},
                                      {"error", "洗车中,不能应用需要重启的配置;请等洗车结束或先停止洗车"}});
    }

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    const json patch = body.contains("patch") ? body["patch"] : body;
    std::string expected;
    if (!readString(body, "expectedRevision", expected)) {
        return errorResponse(400, "expectedRevision 必须是字符串");
    }

    std::string error;
    std::string newRevision;
    if (!ConfigManager::instance().savePatch(patch, expected, username, request.clientIp,
                                             error, newRevision)) {
        LOG_WARNING("[ConfigUI] apply rejected, user=" + username + ", ip=" + request.clientIp + ", error=" + error);
        return jsonResponse(422, json{{"ok", false}, {"error", error}});
    }

    LOG_WARNING("[ConfigUI] config applied, user=" + username + ", ip=" + request.clientIp +
                ", revision=" + newRevision + "; restarting");
    scheduleRestart();
    return jsonResponse(200, json{{"ok", true}, {"revision", newRevision}, {"restarting", true}});
}

HttpResponse AdminController::handleHistory(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    json entries = json::array();
    for (const auto& entry : ConfigManager::instance().history()) {
        entries.push_back(json{{"id", entry.id}, {"time", entry.time}, {"size", entry.size}});
    }
    return jsonResponse(200, json{{"ok", true}, {"entries", entries}});
}

HttpResponse AdminController::handleRollback(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;
    if (m_lobby.isWashCarWashing()) {
        return jsonResponse(409, json{{"ok", false}, {"code", "washing"},
                                      {"error", "洗车中不能回滚配置"}});
    }

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    std::string id;
    if (!readString(body, "id", id)) return errorResponse(400, "id 必须是字符串");
    std::string error;
    std::string newRevision;
    if (!ConfigManager::instance().rollback(id, username, request.clientIp, error, newRevision)) {
        return jsonResponse(422, json{{"ok", false}, {"error", error}});
    }
    LOG_WARNING("[ConfigUI] config rollback, user=" + username + ", ip=" + request.clientIp +
                ", id=" + id + ", revision=" + newRevision);
    scheduleRestart();
    return jsonResponse(200, json{{"ok", true}, {"revision", newRevision}, {"restarting", true}});
}

HttpResponse AdminController::handleTestRead(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    std::string point;
    if (!readString(body, "point", point)) return errorResponse(400, "point 必须是字符串");
    if (point.empty()) return errorResponse(400, "point 不能为空");

    int value = 0;
    std::string error;
    if (!m_device.readPointNow("", point, value, error)) {
        return jsonResponse(422, json{{"ok", false}, {"error", error}, {"point", point}});
    }
    return jsonResponse(200, json{{"ok", true}, {"point", point}, {"value", value}});
}

HttpResponse AdminController::handlePassword(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);
    std::string oldPassword;
    std::string newPassword;
    if (!readString(body, "oldPassword", oldPassword) ||
        !readString(body, "newPassword", newPassword)) {
        return errorResponse(400, "oldPassword / newPassword 必须是字符串");
    }
    if (newPassword.size() < 6) return errorResponse(400, "新密码至少 6 位");
    if (!m_auth.verifyPassword(oldPassword)) return errorResponse(403, "旧密码不正确");

    const std::string hash = AuthService::makePasswordHash(newPassword);
    if (hash.empty()) return errorResponse(500, "生成密码哈希失败");

    std::string error;
    std::string newRevision;
    json patch = json{{"admin", {{"passwordHash", hash}}}};
    if (!ConfigManager::instance().savePatch(patch, ConfigManager::instance().revision(),
                                             username, request.clientIp, error, newRevision)) {
        return jsonResponse(422, json{{"ok", false}, {"error", error}});
    }
    m_auth.setPasswordHashInMemory(hash);
    LOG_WARNING("[ConfigUI] password changed, user=" + username + ", ip=" + request.clientIp);
    return jsonResponse(200, json{{"ok", true}, {"revision", newRevision}});
}

HttpResponse AdminController::handleRestart(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;
    if (m_lobby.isWashCarWashing()) {
        return jsonResponse(409, json{{"ok", false}, {"code", "washing"},
                                      {"error", "洗车中不能重启程序"}});
    }
    LOG_WARNING("[ConfigUI] manual restart requested, user=" + username + ", ip=" + request.clientIp);
    scheduleRestart();
    return jsonResponse(200, json{{"ok", true}, {"restarting", true}});
}

HttpResponse AdminController::handleLogs(const HttpRequest& request) {
    std::string username;
    HttpResponse denied;
    if (!requireLogin(request, username, denied)) return denied;

    json body;
    std::string parseError;
    if (!parseBody(request, body, parseError)) return errorResponse(400, parseError);

    int limit = 300;
    if (!readInt(body, "limit", limit, 300)) return errorResponse(400, "limit 必须是整数");
    std::string date;
    std::string service;
    std::string level;
    std::string keyword;
    if (!readString(body, "date", date) || !readString(body, "service", service, "all") ||
        !readString(body, "level", level) || !readString(body, "keyword", keyword)) {
        return errorResponse(400, "date / service / level / keyword 必须是字符串");
    }

    const std::string dir = m_logDir.empty() ? ConfigManager::instance().logDir() : m_logDir;
    const std::size_t queryLimit = static_cast<std::size_t>(std::max(1, limit));
    const auto result = log_query::query(dir, date, service, level, keyword, queryLimit);

    json dates = json::array();
    for (const auto& date : result.dates) dates.push_back(date);

    json counts = json::object();
    for (const auto& [service, count] : result.counts) {
        counts[service] = count;
    }

    json entries = json::array();
    for (const auto& entry : result.entries) {
        entries.push_back(json{
            {"time", entry.time},
            {"level", entry.level},
            {"service", entry.service},
            {"serviceLabel", log_query::serviceLabel(entry.service)},
            {"message", entry.message}
        });
    }

    return jsonResponse(200, json{
        {"ok", true},
        {"date", result.date},
        {"dates", dates},
        {"counts", counts},
        {"entries", entries},
        {"total", result.total},
        {"truncated", result.truncated},
        {"logDir", dir}
    });
}

void AdminController::scheduleRestart() {
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::raise(SIGTERM);
    }).detach();
}
