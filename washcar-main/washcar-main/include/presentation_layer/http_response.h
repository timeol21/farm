#pragma once
#include <string>
#include <vector>
#include "data_layer/device/device_operation_result.h"
#include "business_layer/lobby/lobby_object.h"
#include <nlohmann/json.hpp>

struct HttpResponse {
    int status = 200;
    std::string body;
    std::string contentType = "application/json; charset=utf-8";
    bool allowCors = true;
    std::vector<std::string> headers;

    static HttpResponse createResponse(const LobbyResult<SolenoidValveOperationResult>& opResult);

};