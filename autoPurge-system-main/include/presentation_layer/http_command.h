#pragma once
#include "business_layer/command/controller.h"




/**
 * @brief Web控制器接口类
 * 处理JSON请求的业务逻辑，与WebService解耦
 */

class HTTPCommandController : public IController{
public:
    
    explicit HTTPCommandController(ILobbyService& lobby);
    
    /**
     * @brief 处理JSON请求
     * @param topic 处理请求
     * @return payload 响应数据
     */
    HttpResponse handle(const std::string& topic, const std::string& payload) override;

    void handleMqtt(const std::string& topic, const std::string& payload) override;

private:
    ILobbyService& lobbyService; 

private:

    HttpResponse handleoperateSolenoidValve(const nlohmann::json& j);

    HttpResponse handleGetRealImage(const nlohmann::json& j);

    // void handleGetSensorData(const nlohmann::json& j);

    HttpResponse handleGetAllDeviceStatus(const nlohmann::json& j);

    HttpResponse handVideoHistoryTime(const nlohmann::json& j);
    
    HttpResponse handVideoHistoryFile(const nlohmann::json& j);



    HttpResponse handleWaterLevelStatus(const nlohmann::json& j);

    HttpResponse handleSmokeDetectorStatus(const nlohmann::json& j);

    HttpResponse handleInfraredSensorStatus(const nlohmann::json& j);

    HttpResponse handleTempHumidSensorStatus(const nlohmann::json& j);

    HttpResponse handleSolenoidStatus(const nlohmann::json& j);

    // void
};
