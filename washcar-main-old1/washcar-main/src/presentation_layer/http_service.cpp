#include "presentation_layer/http_service.h"
#include <cctype>

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
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

WebService::WebService(const std::string& ip,short port,IController& controller)
    : m_bind_ip(ip.empty() ? "0.0.0.0" : ip),
      m_port(port <= 0 ? 8080 : port),
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

        std::thread(&WebService::handleClient, this, client_fd).detach();
    }
}


void WebService::handleClient(int client_fd)
{
    std::string request;

    if (!receiveHttpRequest(client_fd, request)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Invalid Request"})");
        closeConnection(client_fd);
        return;
    }

    std::string method, path, body;

    if (!parseHttpRequest(request, method, path, body)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Parse Failed"})");
        closeConnection(client_fd);
        return;
    }

    // [2026-09-03] OPTIONS 预检处理:浏览器在 CORS POST 前会先发 OPTIONS,
    // 服务端返 204 + CORS headers 表示允许跨域,然后浏览器才发实际 POST。
    // validateRequest 之前短路由,避免被 405 拦截。
    if (method == "OPTIONS") {
        sendHttpResponse(client_fd, 204, "");
        closeConnection(client_fd);
        return;
    }

    // 拆开校验:错方法 → 405,坏 JSON → 400 (合并检查会丢信息)
    if (method != "POST") {
        sendHttpResponse(client_fd, 405, R"({"error":"Method Not Allowed"})");
        closeConnection(client_fd);
        return;
    }

    if (!validateRequest(method, body)) {
        sendHttpResponse(client_fd, 400, R"({"error":"Invalid JSON body"})");
        closeConnection(client_fd);
        return;
    }

    HttpResponse response = dispatchRequest(path, body);//这个应该是需要返回值出来

    sendHttpResponse(client_fd, response.status, response.body);

    closeConnection(client_fd);
}




bool WebService::receiveHttpRequest(int client_fd,std::string& request)
{
    // Bug fix 2026-09-03: 单次 recv 可能拿到部分 HTTP request (TCP 不保证一次完整),
    // 导致 parseHttpRequest 提取的 body 截断,json::parse 失败 -> 405 Invalid Method or Body。
    // 修法:循环 recv 直到拿到完整 header (\r\n\r\n) + Content-Length 字节的 body。
    constexpr size_t buffer_size = 8192;
    char buffer[buffer_size];
    request.clear();

    while (true) {
        ssize_t bytes = recv(client_fd, buffer, buffer_size - 1, 0);
        if (bytes <= 0) {
            if (request.empty()) return false;
            break;  // 已收到部分,跳出用现有数据解析
        }
        buffer[bytes] = '\0';
        request.append(buffer, bytes);

        // 找 header 结尾
        size_t header_end = request.find("\r\n\r\n");
        if (header_end == std::string::npos) continue;  // header 没收完,继续 recv

        // 找 Content-Length(在 header 范围内)
        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos == std::string::npos || cl_pos > header_end) break;  // 没 body 或 body 长度未知

        // 解析 Content-Length 数字
        size_t cl_val_start = request.find_first_of("0123456789", cl_pos);
        if (cl_val_start == std::string::npos || cl_val_start > header_end) break;
        size_t cl_val_end = cl_val_start;
        while (cl_val_end < request.size() && isdigit(static_cast<unsigned char>(request[cl_val_end]))) {
            cl_val_end++;
        }
        int content_length = std::stoi(request.substr(cl_val_start, cl_val_end - cl_val_start));

        // 已有 body 字节数
        size_t body_have = request.size() - (header_end + 4);
        if (body_have >= static_cast<size_t>(content_length)) break;  // body 收齐

        // 继续 recv 收剩余 body
        while (body_have < static_cast<size_t>(content_length)) {
            ssize_t more = recv(client_fd, buffer, buffer_size - 1, 0);
            if (more <= 0) break;  // 连接断开,跳出
            buffer[more] = '\0';
            request.append(buffer, more);
            body_have += more;
        }
        break;  // body 收齐或连接断,跳出
    }

    return !request.empty();
}

bool WebService::parseHttpRequest(const std::string& raw,
                                  std::string& outMethod,
                                  std::string& outPath,
                                  std::string& outBody)
{
    std::istringstream stream(raw);
    stream >> outMethod >> outPath;

    size_t body_pos = raw.find("\r\n\r\n");
    if (body_pos == std::string::npos)
        return false;

    outBody = raw.substr(body_pos + 4);

    return true;
}


bool WebService::validateRequest(const std::string& method,
                                 const std::string& body)
{
    if (method != "POST")
        return false;

    try {
        json parsed = json::parse(body);
    } catch (...) {
        return false;
    }

    return true;
}

HttpResponse WebService::dispatchRequest(const std::string& path,
                                 const std::string& body)
{
    return dispatcher_.handle(path, body);
}

void WebService::sendHttpResponse(int clientFd,
                                  int statusCode,
                                  const std::string& body)
{
    std::ostringstream response;

    response << "HTTP/1.1 " << statusCode << " " << reasonFor(statusCode) << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";
    response << "\r\n";
    response << body;

    std::string resp = response.str();
    send(clientFd, resp.c_str(), resp.size(), 0);
}



void WebService::closeConnection(int clientFd)
{
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
}
