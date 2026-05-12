#include "presentation_layer/http_command.h"
#include "common/log/log_manager.h"
HTTPCommandController::HTTPCommandController(ILobbyService& lobby) : lobbyService(lobby) {
    // 构造函数，初始化lobbyService引用
}
// public:

HttpResponse HTTPCommandController::handle(const std::string& topic, const std::string& payload){
    HttpResponse result;
    nlohmann::json j = nlohmann::json::parse(payload);
    if(topic == GET_REAL_IMAGE_TOPIC){
        result = handleGetRealImage(j);  
    } /*else if(topic == OPERATE_PLC_WITH_VERIFY_TOPIC){
        result = handleoperateSolenoidValve(j); 
    }*/
    return result;
}
void HTTPCommandController::handleMqtt(const std::string& topic, const std::string& payload){

}
// private:



HttpResponse HTTPCommandController::handleGetRealImage(const nlohmann::json& j){
    FrameQuery query(j);
    if(!query.isValid()){
        // 处理无效查询，例如返回错误响应
        return {400, R"({"error":"Invalid parameters"})"};
    }
    auto result = lobbyService.retrieveLiveCameraFrame(query);
    return HttpResponse{};
}

//HttpResponse HTTPCommandController::handleoperateSolenoidValve(const nlohmann::json& j){
//    try {
//        SolenoidValveOperation op(j);
//
//        if (!op.isValid()) {
//            return {400, R"({"error":"Invalid parameters"})"};
//        }
//
//        auto result = lobbyService.operateSolenoidValve(op);
//
//        
//        return HttpResponse::createResponse(result);
//
//    } catch (const std::exception& e) {
//        LOG_ERROR(std::string("HTTP exception: ") + e.what());
//        return {
//            500,
//            R"({"success":false,"errorCode":500,"message":"Internal server error"})"
//        };
//    }   
//}

// void HTTPCommandController::handleGetSensorData(const nlohmann::json& j){

// }

HttpResponse HTTPCommandController::handleGetAllDeviceStatus(const nlohmann::json& j){
    DeviceStatusQuery query(j);
    if(!query.isValid()){
        // 处理无效查询，例如返回错误响应
        return {400, R"({"error":"Invalid parameters"})"};
    }
    auto result = lobbyService.retrieveDeviceStatus(query);   
    return HttpResponse{};
}

HttpResponse HTTPCommandController::handVideoHistoryTime(const nlohmann::json& j){
    HistoricalVideoQuery query(j);
    if(!query.isValid()){
        // 处理无效查询，例如返回错误响应
        return {400, R"({"error":"Invalid parameters"})"};
    }
    auto result = lobbyService.retrieveHistoricalCameraFootage(query);    
    return HttpResponse{};
}
    
HttpResponse HTTPCommandController::handVideoHistoryFile(const nlohmann::json& j){
    DownloadHistoricalVideo download(j);
    if(!download.isValid()){
        // 处理无效查询，例如返回错误响应
        return {400, R"({"error":"Invalid parameters"})"};
    }
    auto result = lobbyService.downloadHistoricalCameraFootage(download);     
    return HttpResponse{};
}