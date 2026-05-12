#pragma once

class ILobbyService; // 替代 #include "lobby_service.h"
class HttpResponse; // 替代 #include "http_response.h"
class IController;
#include "business_layer/command/controller.h"
#include "common/topics.h"
#include "business_layer/lobby/lobby_object.h"
#include <nlohmann/json.hpp>



class MQTTCommandController : public IController{
public:
    
    explicit MQTTCommandController(ILobbyService& lobby);
    
    ~MQTTCommandController() = default;
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

    void handleUpdateConfig(const nlohmann::json& j); //更新配置文件

    void handleGetAllDeviceStatus(const nlohmann::json& j);//所有设备状态

    void handleOperateWithVerify(const nlohmann::json& j);//电磁阀操作


    // void handVideoHistoryTime(const nlohmann::json& j);
    
    // void handVideoHistoryFile(const nlohmann::json& j);


    // void
};