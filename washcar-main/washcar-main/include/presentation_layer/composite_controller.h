#pragma once

#include "business_layer/command/controller.h"

// 把命令 API 和配置台 API 挂在同一个 8080 端口上。
// /api/* 走配置台;其余路径保持原来的命令入口。
class CompositeController : public IController {
public:
    CompositeController(IController& command, IController& config)
        : m_command(command), m_config(config) {}

    HttpResponse handle(const HttpRequest& request) override {
        if (request.path.rfind("/api/", 0) == 0) return m_config.handle(request);
        return m_command.handle(request);
    }

    HttpResponse handleHttp(const std::string& path, const std::string& payload) override {
        if (path.rfind("/api/", 0) == 0) return m_config.handleHttp(path, payload);
        return m_command.handleHttp(path, payload);
    }

    void handleMqtt(const std::string& topic, const std::string& payload) override {
        m_command.handleMqtt(topic, payload);
    }

private:
    IController& m_command;
    IController& m_config;
};
