#pragma once
#include <string>
// #include "data_layer/device/device_operation_result.h"
#include "business_layer/lobby/lobby_object.h"

#include <nlohmann/json.hpp>


struct HttpResponse {
    int statusCode = 200;
    std::string reasonPhrase = "OK";
    std::string contentType = "application/json";
    std::vector<unsigned char> body;
    std::map<std::string, std::string> headers;

    static HttpResponse Json(int statusCode, const std::string& json);
    static HttpResponse Text(int statusCode, const std::string& text);
    static HttpResponse Binary(int statusCode,const std::string& contentType,const std::vector<unsigned char>& data);

    static HttpResponse fromError(ErrorCode::Code code, const std::string& message);

    static int mapToHttpStatus(ErrorCode::Code code);

    template<typename T>
    static HttpResponse fromLobbyResult(const LobbyResult<T>& result){
        nlohmann::json resp;

        if (result.success) {
            resp["success"] = true;
            resp["data"] = toJson(*result.data);
            return Json(200, resp);
        }

        resp["success"] = false;
        resp["errorCode"] = static_cast<int>(result.errorCode);
        resp["message"] = result.message;

        int status = mapToHttpStatus(result.errorCode);
        return Json(status, resp);
    }


    // static nlohmann::json toJson(const SolenoidValveOperationResult& obj);

    // static nlohmann::json toJson(const SolenoidStatus& obj);

    // static nlohmann::json toJson(const TempHumidSensorStatus& obj);

    // static nlohmann::json toJson(const InfraredSensorStatus& obj);

    // static nlohmann::json toJson(const SmokeDetectorStatus& obj);

    // static nlohmann::json toJson(const WaterLevelSensorStatus& obj);

};