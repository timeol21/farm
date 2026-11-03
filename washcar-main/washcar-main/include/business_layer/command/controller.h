#pragma once
#include <string>
#include <vector>

#include "business_layer/lobby/lobby_service.h"
#include "common/topics.h"
#include "business_layer/lobby/lobby_object.h"
#include <nlohmann/json.hpp>
#include "presentation_layer/http_response.h"
#include "presentation_layer/http_request.h"

using nlohmann::json;
class IController{ //适配器 
public:

    virtual HttpResponse handleHttp(const std::string& topic, const std::string& payload) = 0;

    // 新版 HTTP 入口。默认退回旧接口,现有命令控制器不用改。
    virtual HttpResponse handle(const HttpRequest& request) {
        return handleHttp(request.path, request.body);
    }

    // 需要订阅的 MQTT 主题。命令服务是唯一来源, 传输层照此订阅。
    virtual std::vector<std::string> subscriptionTopics() const { return {}; }

    virtual ~IController() = default;

    virtual void handleMqtt(const std::string& topic, const std::string& payload) = 0;
};
