#include "http_client.h"



std::string HttpClient::postJson(const std::string& url, const nlohmann::json& body) {
        return httpRequest("POST", url, body.dump());
}

 std::string HttpClient::get(const std::string& url) {
        return httpRequest("GET", url, "");
}

std::string HttpClient::httpRequest(const std::string& method,const std::string& url,const std::string& body){
        // -----------------------------
        // 1. 解析 URL: host, port, path
        // -----------------------------
        std::string host, path;
        int port = 80;

        if (!parseUrl(url, host, port, path)) {
            return "invalid_url";
        }

        // -----------------------------
        // 2. 建立 socket 连接
        // -----------------------------
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "socket_error";

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            ::close(sock);
            return "invalid_host";
        }

        if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(sock);
            return "connect_failed";
        }

        // -----------------------------
        // 3. 构造 HTTP 请求报文
        // -----------------------------
        std::ostringstream req;
        req << method << " " << path << " HTTP/1.1\r\n";
        req << "Host: " << host << "\r\n";

        if (method == "POST") {
            req << "Content-Type: application/json\r\n";
            req << "Content-Length: " << body.size() << "\r\n";
        }

        req << "Connection: close\r\n\r\n";

        if (method == "POST") {
            req << body;
        }

        std::string reqStr = req.str();
        send(sock, reqStr.c_str(), reqStr.size(), 0);

        // -----------------------------
        // 4. 接收 HTTP 响应
        // -----------------------------
        char buffer[4096];
        std::string response;

        ssize_t n;
        while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            response.append(buffer, n);
        }

        ::close(sock);

        // -----------------------------
        // 5. 去掉 HTTP 头，只返回 Body
        // -----------------------------
        auto pos = response.find("\r\n\r\n");
        if (pos != std::string::npos) {
            return response.substr(pos + 4);
        }
        return response;
}

bool HttpClient::parseUrl(const std::string& url,std::string& host,int& port,std::string& path){
    
    if (url.rfind("http://", 0) == 0) {
        std::string u = url.substr(7);

        size_t slashPos = u.find('/');
        if (slashPos == std::string::npos) return false;

        std::string hostPort = u.substr(0, slashPos);
        path = u.substr(slashPos);

        size_t colonPos = hostPort.find(':');
        if (colonPos != std::string::npos) {
            host = hostPort.substr(0, colonPos);
            port = std::stoi(hostPort.substr(colonPos + 1));
        } else {
            host = hostPort;
            port = 80;
        }
        return true;
    }
    return false;
}