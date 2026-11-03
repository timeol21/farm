#pragma once

#include <string>

// 一次 HTTP 请求的最小上下文。控制器需要 Cookie(登录态)和客户端 IP(审计),
// 所以不能只传 path/body。
struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::string cookie;
    std::string clientIp;
};
