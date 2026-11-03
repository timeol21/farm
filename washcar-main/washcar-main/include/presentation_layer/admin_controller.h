#pragma once

#include "business_layer/command/controller.h"
#include "presentation_layer/http_request.h"
#include "presentation_layer/http_response.h"

#include <string>

class ILobbyService;
class IDeviceService;
class AuthService;

// 现场运维配置台的 HTTP 适配层。只服务单盒、局域网访问。
class AdminController : public IController {
public:
    AdminController(ILobbyService& lobby, IDeviceService& device, AuthService& auth,
                     const std::string& logDir = "");

    HttpResponse handle(const HttpRequest& request) override;
    HttpResponse handleHttp(const std::string& path, const std::string& payload) override;
    void handleMqtt(const std::string& topic, const std::string& payload) override;

private:
    // 路由分发单独一层:handleHttp() 外面包异常兜底,任何一条畸形请求都不能
    // 把异常抛到 HTTP 工作线程外(线程函数抛异常 = std::terminate,
    // 整个盒子进程 abort,PLC 采集和云端链路一起断)。
    HttpResponse route(const HttpRequest& request);

    HttpResponse handleLogin(const HttpRequest& request);
    HttpResponse handleLogout(const HttpRequest& request);
    HttpResponse handleSession(const HttpRequest& request);
    HttpResponse handleStatus(const HttpRequest& request);
    HttpResponse handleConfig(const HttpRequest& request);
    HttpResponse handleValidate(const HttpRequest& request);
    HttpResponse handleApply(const HttpRequest& request);
    HttpResponse handleHistory(const HttpRequest& request);
    HttpResponse handleRollback(const HttpRequest& request);
    HttpResponse handleTestRead(const HttpRequest& request);
    HttpResponse handlePassword(const HttpRequest& request);
    HttpResponse handleRestart(const HttpRequest& request);
    HttpResponse handleLogs(const HttpRequest& request);

    bool requireLogin(const HttpRequest& request, std::string& username, HttpResponse& denied) const;
    void scheduleRestart();

    ILobbyService& m_lobby;
    IDeviceService& m_device;
    AuthService& m_auth;
    std::string m_logDir;
};
