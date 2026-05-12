#pragma once
#include <string>
#include "business_layer/lobby/lobby_service.h"
#include "common/topics.h"
#include "business_layer/lobby/lobby_object.h"
#include <nlohmann/json.hpp>
#include "presentation_layer/http_response.h"

using nlohmann::json;
class IController{ //适配器 
public:

    virtual HttpResponse handle(const std::string& topic, const std::string& payload) = 0;

    virtual ~IController() = default;

    virtual void handleMqtt(const std::string& topic, const std::string& payload) = 0;
};
