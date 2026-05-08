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
    } else if(topic == OPERATE_PLC_WITH_VERIFY_TOPIC){
        result = handleoperateSolenoidValve(j); 
    } else if(topic == GET_DEVICE_SOLENOID_STATUS){
        result = handleWaterLevelStatus(j);
    }  else if(topic == GET_DEVICE_TEMP_HUMID_SENSOR_STATUS){
        result = handleSmokeDetectorStatus(j);
    } else if(topic == GET_DEVICE_INFRARED_SENSOR_STATUS){
        result = handleInfraredSensorStatus(j);
    } else if(topic == GET_DEVICE_SMOKE_DETECTOR_STATUS){
        result = handleTempHumidSensorStatus(j);
    } else if(topic == GET_DEVICE_WATER_LEVEL_STATUS){
        result = handleSolenoidStatus(j);   
    }
    return result;
}

void HTTPCommandController::handleMqtt(const std::string& topic, const std::string& payload){

}
// private:



HttpResponse HTTPCommandController::handleGetRealImage(const nlohmann::json& j){

    // try {
    //     FrameQuery query(j);
    //     if(!query.isValid()){
    //         // 处理无效查询，例如返回错误响应
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }
    //     auto result = lobbyService.retrieveLiveCameraFrame(query);

    //     return HttpResponse::Binary(200, "image/jpeg", result.data->getJpegData());
    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // } 
    return HttpResponse();
}

HttpResponse HTTPCommandController::handleoperateSolenoidValve(const nlohmann::json& j){
    // try {
    //     SolenoidValveOperation op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.operateSolenoidValve(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }
    return HttpResponse();   
}

// void HTTPCommandController::handleGetSensorData(const nlohmann::json& j){

// }
HttpResponse HTTPCommandController::handleSolenoidStatus(const nlohmann::json& j){
    // try {
    //     DeviceStatusQuery op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.retrieveSolenoidStatus(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }   
    return HttpResponse();
}

HttpResponse HTTPCommandController::handleTempHumidSensorStatus(const nlohmann::json& j){
    // try {
    //     DeviceStatusQuery op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.retrieveTempHumidSensorStatus(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }  
    return HttpResponse(); 
}

HttpResponse HTTPCommandController::handleInfraredSensorStatus(const nlohmann::json& j){
    // try {
    //     DeviceStatusQuery op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.retrieveInfraredSensorStatus(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }   
    return HttpResponse();
}

HttpResponse HTTPCommandController::handleSmokeDetectorStatus(const nlohmann::json& j){
    // try {
    //     DeviceStatusQuery op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.retrieveSmokeDetectorStatus(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }   
    return HttpResponse();
}

HttpResponse HTTPCommandController::handleWaterLevelStatus(const nlohmann::json& j){
    // try {
    //     DeviceStatusQuery op(j);

    //     if (!op.isValid()) {
    //         return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    //     }

    //     auto result = lobbyService.retrieveWaterLevelStatus(op);

        
    //     return HttpResponse::fromLobbyResult(result);

    // } catch (const std::exception& e) {
    //     LOG_ERROR(std::string("HTTP exception: ") + e.what());
    //     return {
    //         HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request")
    //     };
    // }   
    return HttpResponse();
}


HttpResponse HTTPCommandController::handleGetAllDeviceStatus(const nlohmann::json& j){
    // DeviceStatusQuery query(j);
    // if(!query.isValid()){
    //     // 处理无效查询，例如返回错误响应
    //     return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    // }
    // auto result = lobbyService.retrieveDeviceStatus(query);   



    return HttpResponse();
}



HttpResponse HTTPCommandController::handVideoHistoryTime(const nlohmann::json& j){
    // HistoricalVideoQuery query(j);
    // if(!query.isValid()){
    //     // 处理无效查询，例如返回错误响应
    //     return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    // }
    // auto result = lobbyService.retrieveHistoricalCameraFootage(query);    
    // return HttpResponse{};
    return HttpResponse();
}
    
HttpResponse HTTPCommandController::handVideoHistoryFile(const nlohmann::json& j){
    // DownloadHistoricalVideo download(j);
    // if(!download.isValid()){
    //     // 处理无效查询，例如返回错误响应
    //     return HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request");
    // }
    // auto result = lobbyService.downloadHistoricalCameraFootage(download);     
    // return HttpResponse{};
    return HttpResponse();
}