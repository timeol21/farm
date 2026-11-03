// http_test.cpp — HTTP 服务自检。
// 覆盖: 静态文件、JS MIME、health、目录穿越拦截、POST 路由到控制器、
//       控制器抛异常回 500 且服务还能接着干活。
// 用法: ./http_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "presentation_layer/http_service.h"
#include "presentation_layer/http_request.h"

#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int g_pass = 0;
int g_fail = 0;

void expect(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::cout << "  [FAIL] " << what << std::endl;
}

class TestController : public IController {
public:
    HttpResponse handleHttp(const std::string& path, const std::string& payload) override {
        HttpResponse r;
        r.status = 200;
        r.body = std::string("{\"legacy\":\"") + path + "\"}";
        return r;
    }
    HttpResponse handle(const HttpRequest& request) override {
        // 模拟控制器内部出错(比如配置台里 JSON 字段类型不对)。
        // 这条路径以前会把异常带出工作线程,整个盒子进程 std::terminate。
        if (request.path == "/boom") {
            throw std::runtime_error("controller blew up");
        }
        HttpResponse r;
        r.status = 200;
        r.contentType = "application/json; charset=utf-8";
        r.body = std::string("{\"path\":\"") + request.path + "\",\"cookie\":\"" + request.cookie + "\"}";
        return r;
    }
    void handleMqtt(const std::string&, const std::string&) override {}
};

std::string httpRequest(int port, const std::string& raw) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return {};
    send(fd, raw.data(), raw.size(), 0);
    std::string out;
    char buf[2048];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, n);
    }
    close(fd);
    return out;
}

}  // namespace

int main() {
    const std::string root = "/tmp/washcar_web_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    { std::ofstream(root + "/index.html") << "<html>ok</html>"; }
    { std::ofstream(root + "/app.js") << "console.log(1);"; }

    TestController controller;
    WebService service("127.0.0.1", 18080, controller, root);
    if (!service.start()) {
        std::cout << "  [FAIL] start" << std::endl;
        return 1;
    }

    std::string r = httpRequest(18080, "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    expect(r.find("200 OK") != std::string::npos && r.find("<html>ok</html>") != std::string::npos,
           "serve index");
    r = httpRequest(18080, "GET /app.js HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    expect(r.find("application/javascript") != std::string::npos && r.find("console.log") != std::string::npos,
           "serve js");
    r = httpRequest(18080, "GET /api/health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    expect(r.find("200 OK") != std::string::npos && r.find("\"ok\":true") != std::string::npos, "health");
    r = httpRequest(18080, "GET /../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    expect(r.find("404") != std::string::npos, "path traversal blocked");
    r = httpRequest(18080,
                    "POST /api/x HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                    "Content-Length: 2\r\nCookie: washcar_session=abc\r\nConnection: close\r\n\r\n{}");
    expect(r.find("200 OK") != std::string::npos &&
               r.find("\"cookie\":\"washcar_session=abc\"") != std::string::npos,
           "post routes to controller with cookie");

    // 控制器抛异常:必须回 500,而且服务还得活着接着干活。
    // 以前异常从工作线程逃出去 = std::terminate,整个盒子进程 abort,
    // PLC 采集、云端链路、正在进行的洗车一起断。
    r = httpRequest(18080,
                    "POST /boom HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
    expect(r.find("500") != std::string::npos, "throwing controller returns 500");
    r = httpRequest(18080, "GET /api/health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    expect(r.find("200 OK") != std::string::npos && r.find("\"ok\":true") != std::string::npos,
           "server still serving after a controller exception");

    service.stop();
    std::filesystem::remove_all(root);

    std::cout << "http_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
