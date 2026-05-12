#include "business_layer/command/mqtt_command.h"
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_response.h"
#include <nlohmann/json.hpp>
#include "common/log/log_manager.h"
MQTTCommandController::MQTTCommandController(ILobbyService& lobby) : lobbyService(lobby) {}

HttpResponse MQTTCommandController::handle(const std::string& topic, const std::string& payload) {
    return HttpResponse{}; 
}

void MQTTCommandController::handleMqtt(const std::string& topic, const std::string& payload) {
    auto j = json::parse(payload);
    if(topic == UPDATE_CONFIG_TOPIC ){
        handleUpdateConfig( j);
    } else if(topic == GET_ALL_DEVICE_STATUS_TOPIC){
        handleGetAllDeviceStatus( j);
    } /*else if(topic == OPERATE_PLC_WITH_VERIFY_TOPIC){
        handleOperateWithVerify( j);
    }*/
}


void MQTTCommandController::handleUpdateConfig(const nlohmann::json& j){
    
} 

void MQTTCommandController::handleGetAllDeviceStatus(const nlohmann::json& j){

}

//void MQTTCommandController::handleOperateWithVerify(const nlohmann::json& j){
//    try {
//        SolenoidValveOperation operation(j);
//        if (!operation.isValid()) {
//            LOG_ERROR("SolenoidValveOperation: 缺少必要字段");
//            return;
//        }
//        auto result = lobbyService.operateSolenoidValve(operation);
//        if (!result.success) {
//            LOG_ERROR("Operate solenoid valve 失败");
//        }
//    } catch (const std::exception& e) {
//        LOG_ERROR(std::string("Exception: ") + e.what());
//    }
//}
/*
void MQTTCommandController::handleOperateWithVerify(const nlohmann::json& j){
    try {
       
        SolenoidValveOperation operation(j);

        if (operation.isValid()) {
            LOG_ERROR(" SolenoidValveOperation: cmd 为空");
            return;
        }

        auto result = lobbyService.operateSolenoidValve(operation);

        if (!result.success) {
            LOG_ERROR("Operate solenoid valve 失败");
            return;
        }

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Exception: ") + e.what());
    }
}*/