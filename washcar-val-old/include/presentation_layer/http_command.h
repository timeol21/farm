#pragma once
#include "business_layer/command/controller.h"




/**
 * @brief Web控制器接口类
 * 处理JSON请求的业务逻辑，与WebService解耦
 */

class HTTPCommandController : public IController {
public:

    explicit HTTPCommandController(ILobbyService& lobby);

    /**
     * @brief 处理JSON请求
     * @param path 请求路径
     * @param payload 响应数据
     * @return HttpResponse
     */
    HttpResponse handle(const std::string& path, const std::string& payload) override;

    void handleMqtt(const std::string& topic, const std::string& payload) override;

private:
    ILobbyService& lobbyService;

private:
    HttpResponse handleWashCarStart(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);// 开始洗车

    HttpResponse handleWashCarStop(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarReset(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarStatus(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarReadData(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarChangeMode(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarClearAlarm(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarPoint(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    HttpResponse handleWashCarQuery(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

    // 驿公里独有命令
    HttpResponse handleWashCarAbort(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
    HttpResponse handleWashCarStopWork(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
    HttpResponse handleWashCarStopService(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
    HttpResponse handleWashCarResetSafe(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);

	//点位操作
	HttpResponse handlePointOnHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
	HttpResponse handlePointOffHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
	HttpResponse handlePointResetHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
	HttpResponse handlePointStatusHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
	HttpResponse handleClearAlarmHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum);
};
