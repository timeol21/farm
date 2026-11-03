#include "presentation_layer/http_service.h"
#include <cctype>
#include <fstream>
#include <filesystem>
#include <vector>

namespace {

// 并发连接上限。配置台是常驻进程,同一进程还跑着 PLC 采集和 MQTT 收发,
// 每条连接一个 detached 线程又不设上限的话,几百条慢连接就能把线程占满,
// 连带影响洗车控制。
constexpr int kMaxConcurrentConnections = 32;

// 静态页单个文件的上限。web/admin 下就三个几十 KB 的文件,
// 但要是有人请求了一个大文件,整份读进内存会把 4GB 的盒子吃光。
constexpr std::uintmax_t kMaxStaticFileBytes = 4 * 1024 * 1024;

// 活跃连接计数守卫:连接进来加一,退出减一。
// 加一在 acceptLoop 里做(跟上限判断合成一次原子操作),这里只负责减。
class ConnectionGuard {
public:
    explicit ConnectionGuard(std::atomic<int>& counter) : m_counter(counter) {}
    ~ConnectionGuard() { --m_counter; }
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
private:
    std::atomic<int>& m_counter;
};

}  // namespace

// HTTP status code → reason phrase。RFC 7230 §3.1.2 要求 reason phrase 跟 status code 匹配。
static std::string reasonFor(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

WebService::WebService(const std::string& ip,short port,IController& controller,
                         const std::string& webRoot)
    : m_bind_ip(ip.empty() ? "0.0.0.0" : ip),
      m_port(port <= 0 ? 8080 : port),
      m_webRoot(webRoot),
      dispatcher_(controller),
      running_(false)
{

}

WebService::~WebService() {
    stop();
}

//public:

bool WebService::start(){

    if (running_) return true;

    if(!initializeServerSocket()) {

        return false;

    }

    running_ = true;

    acceptThread_ = std::thread(&WebService::acceptLoop, this);

    return true;
}   

void WebService::stop(){

    if(!running_) return;

    running_ = false;

    if(m_server_fd != -1){
        shutdown(m_server_fd, SHUT_RDWR);

        close(m_server_fd);

        m_server_fd = -1;
    }

    if(acceptThread_.joinable()){
        acceptThread_.join();
    }  
}

bool WebService::isRunning() const{

    return running_;

}

// private::    生命周期的

bool WebService::initializeServerSocket()
{
    m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_fd < 0)
        return false;

    if (!configureSocket())
        return false;

    if (!bindSocket())
        return false;

    if (!startListening())
        return false;

    return true;
}


bool WebService::configureSocket()
{
    int opt = 1;
    return setsockopt(m_server_fd,SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT,&opt,sizeof(opt)) == 0;
}



bool WebService::bindSocket()
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);

    if (m_bind_ip == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
    else if (inet_pton(AF_INET, m_bind_ip.c_str(), &addr.sin_addr) <= 0)
        return false;

    return bind(m_server_fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) == 0;
}

bool WebService::startListening()
{
    return listen(m_server_fd, SOMAXCONN) == 0;
}



void WebService::acceptLoop()
{
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(m_server_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &len);

        if (client_fd < 0) {
            if (running_)
                continue;
            else
                break;
        }

        // 连接数超上限直接拒掉:不能再 fork 线程,否则并发会被拉爆。
        // fetch_add 一次做完"判断 + 占位",避免多个 accept 之间抢跑超限。
        if (m_activeConnections.fetch_add(1) + 1 > kMaxConcurrentConnections) {
            m_activeConnections.fetch_sub(1);
            sendHttpResponse(client_fd, 503, R"({"error":"Too many connections"})");
            closeConnection(client_fd);
            continue;
        }
        try {
            std::thread(&WebService::handleClient, this, client_fd).detach();
        } catch (const std::exception&) {
            m_activeConnections.fetch_sub(1);
            closeConnection(client_fd);
            continue;
        }
    }
}


void WebService::handleClient(int client_fd)
{
    ConnectionGuard guard(m_activeConnections);
    try {
        handleClientImpl(client_fd);
    } catch (const std::exception& e) {
        // 异常必须在这里兜住:工作线程里未捕获的异常 = std::terminate,
        // 整个盒子进程(PLC 采集 + 云端链路 + 正在进行的洗车)一起断。
        // 控制器出一个错不该让整台机器停机。
        std::cerr << "[Web] request handler failed: " << e.what() << std::endl;
        sendHttpResponse(client_fd, 500, R"({"error":"Internal error"})");
    } catch (...) {
        std::cerr << "[Web] request handler failed: unknown error" << std::endl;
        sendHttpResponse(client_fd, 500, R"({"error":"Internal error"})");
    }
    closeConnection(client_fd);
}


void WebService::handleClientImpl(int client_fd)
{
    std::string raw;
    if (!receiveHttpRequest(client_fd, raw)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Invalid Request"})");
        return;
    }

    HttpRequest request;
    if (!parseHttpRequest(raw, request)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Parse Failed"})");
        return;
    }

    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);
    if (getpeername(client_fd, reinterpret_cast<sockaddr*>(&peer), &peerLen) == 0) {
        char ip[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip))) request.clientIp = ip;
    }

    if (request.method == "OPTIONS") {
        sendHttpResponse(client_fd, 204, "");
        return;
    }

    if (request.method == "GET" || request.method == "HEAD") {
        if (request.path == "/api/health") {
            sendHttpResponse(client_fd, 200, R"({"ok":true,"service":"washcar-config"})");
            return;
        }
        if (serveStatic(client_fd, request.path)) return;
        sendHttpResponse(client_fd, 404, R"({"error":"Not found"})", "application/json; charset=utf-8", false);
        return;
    }

    if (request.method != "POST") {
        sendHttpResponse(client_fd, 405, R"({"error":"Method Not Allowed"})", "application/json; charset=utf-8", false);
        return;
    }

    if (!validateRequest(request.method, request.body)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Invalid JSON body"})", "application/json; charset=utf-8", false);
        return;
    }

    HttpResponse response = dispatchRequest(request);
    sendHttpResponse(client_fd, response.status, response.body, response.contentType,
                      response.allowCors, response.headers);
}

bool WebService::receiveHttpRequest(int client_fd, std::string& request)
{
    constexpr size_t buffer_size = 8192;
    char buffer[buffer_size];
    request.clear();

    timeval tv{10, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {
        ssize_t bytes = recv(client_fd, buffer, buffer_size - 1, 0);
        if (bytes <= 0) {
            if (request.empty()) return false;
            break;
        }
        buffer[bytes] = '\0';
        request.append(buffer, bytes);

        size_t header_end = request.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            if (request.size() > 64 * 1024) return false;
            continue;
        }

        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos == std::string::npos || cl_pos > header_end) break;

        size_t cl_val_start = request.find_first_of("0123456789", cl_pos);
        if (cl_val_start == std::string::npos || cl_val_start > header_end) break;
        size_t cl_val_end = cl_val_start;
        while (cl_val_end < request.size() && isdigit(static_cast<unsigned char>(request[cl_val_end]))) {
            cl_val_end++;
        }
        long content_length = 0;
        try {
            content_length = std::stol(request.substr(cl_val_start, cl_val_end - cl_val_start));
        } catch (...) {
            return false;
        }
        if (content_length < 0 || content_length > 2 * 1024 * 1024) return false;

        size_t body_have = request.size() - (header_end + 4);
        while (body_have < static_cast<size_t>(content_length)) {
            ssize_t more = recv(client_fd, buffer, buffer_size - 1, 0);
            if (more <= 0) break;
            buffer[more] = '\0';
            request.append(buffer, more);
            body_have += static_cast<size_t>(more);
        }
        break;
    }

    return !request.empty();
}

bool WebService::parseHttpRequest(const std::string& raw, HttpRequest& request)
{
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) return false;

    std::istringstream line(raw.substr(0, lineEnd));
    std::string target;
    line >> request.method >> target;
    if (request.method.empty() || target.empty()) return false;

    const size_t queryPos = target.find('?');
    if (queryPos == std::string::npos) {
        request.path = target;
    } else {
        request.path = target.substr(0, queryPos);
        request.query = target.substr(queryPos + 1);
    }

    const size_t bodyPos = raw.find("\r\n\r\n");
    if (bodyPos == std::string::npos) return false;
    request.body = raw.substr(bodyPos + 4);

    const size_t cookiePos = raw.find("\r\nCookie:");
    if (cookiePos != std::string::npos) {
        const size_t valueStart = cookiePos + 9;
        size_t valueEnd = raw.find("\r\n", valueStart);
        if (valueEnd == std::string::npos) valueEnd = raw.size();
        request.cookie = raw.substr(valueStart, valueEnd - valueStart);
        while (!request.cookie.empty() && request.cookie.front() == ' ') request.cookie.erase(request.cookie.begin());
    }
    return true;
}

bool WebService::validateRequest(const std::string& method, const std::string& body)
{
    if (method != "POST") return false;
    try {
        json parsed = json::parse(body);
        (void)parsed;
    } catch (...) {
        return false;
    }
    return true;
}

HttpResponse WebService::dispatchRequest(const HttpRequest& request)
{
    return dispatcher_.handle(request);
}

static std::string mimeTypeFor(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

bool WebService::serveStatic(int clientFd, const std::string& path)
{
    if (m_webRoot.empty()) return false;

    std::string rel = path;
    if (rel == "/" || rel == "/admin" || rel == "/admin/") {
        rel = "/index.html";
    } else if (rel.rfind("/admin/", 0) == 0) {
        rel = rel.substr(6);
    }
    if (rel.empty() || rel[0] != '/') return false;
    if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos) return false;
    for (unsigned char c : rel) {
        if (!(std::isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-')) return false;
    }

    const std::filesystem::path full = std::filesystem::path(m_webRoot) / rel.substr(1);
    // 读之前先看大小:整个文件读进内存,大文件能把 4GB 的盒子吃光,
    // 连带 PLC 采集和 MQTT 一起卡住。
    std::error_code sizeEc;
    const std::uintmax_t fileSize = std::filesystem::file_size(full, sizeEc);
    if (sizeEc || fileSize > kMaxStaticFileBytes) return false;

    std::ifstream in(full, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string body = ss.str();

    sendHttpResponse(clientFd, 200, body, mimeTypeFor(rel), false);
    closeConnection(clientFd);
    return true;
}

void WebService::sendHttpResponse(int clientFd, int statusCode, const std::string& body,
                                  const std::string& contentType, bool allowCors,
                                  const std::vector<std::string>& headers)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << reasonFor(statusCode) << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "Cache-Control: no-store\r\n";
    response << "X-Content-Type-Options: nosniff\r\n";
    response << "X-Frame-Options: DENY\r\n";
    if (allowCors) {
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
    }
    for (const auto& header : headers) response << header << "\r\n";
    response << "\r\n";
    response << body;

    const std::string data = response.str();
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(clientFd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

void WebService::closeConnection(int clientFd)
{
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
}