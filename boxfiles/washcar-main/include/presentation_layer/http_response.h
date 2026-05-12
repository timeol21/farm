#pragma once
#include <string>
#include "data_layer/device/device_operation_result.h"
#include "business_layer/lobby/lobby_object.h"
#include <nlohmann/json.hpp>

struct HttpResponse {
    int status;
    std::string body;

    static HttpResponse createResponse(const LobbyResult<SolenoidValveOperationResult>& opResult);

};