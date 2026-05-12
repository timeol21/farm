#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "presentation_layer/http_command.h"
#include <iostream>
#include <thread>
#include <csignal>

// 全局退出标志
bool g_running = true;

void signalHandler(int) {
    g_running = false;
}

//// 极简测试控制器，不依赖任何业务
class TestController : public IController {
public:
    HttpResponse handle(const std::string& path, const std::string& body) override {
        // 返回网页
        if (path == "/" || path == "/index") {
            return {200, R"(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>控制页面</title></head>
<body><h1>HTTP 服务运行成功</h1></body>
</html>
            )"};
        }
        // 接口
        return {200, R"({"code":0,"msg":"ok"})"};
    }

    void handleMqtt(const std::string& topic, const std::string& payload) {

    }
};

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << ">>> 启动 HTTP 服务：0.0.0.0:8080" << std::endl;

    // 只跑这个！
    TestController controller;

    WebService webService("0.0.0.0", 8080, controller);

    if (!webService.start()) {
        std::cerr << ">>> HTTP 启动失败！" << std::endl;
        return -1;
    }

    std::cout << ">>> 服务已启动，浏览器访问：http://192.168.18.131:8080" << std::endl;

    // 保持运行
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    webService.stop();
    return 0;
}