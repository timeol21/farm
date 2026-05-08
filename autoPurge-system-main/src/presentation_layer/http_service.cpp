#include "presentation_layer/http_service.h"

WebService::WebService(IController& controller)
    : dispatcher_(controller),
      running_(false)
{
    auto services = SystemConfig::instance().getServices();
    web_service_config = services.web_service;



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
    addr.sin_port = htons(web_service_config.port);

    if (web_service_config.ip == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
    else if (inet_pton(AF_INET, web_service_config.ip.c_str(), &addr.sin_addr) <= 0)
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
        sendHttpResponse(client_fd, HttpResponse::fromError(ErrorCode::Code::WEB_INVALID_REQUEST,"Invalid HTTP request"));
        closeConnection(client_fd);
        return;
    }
    
    std::string method, path, body;

    if (!parseHttpRequest(request, method, path, body)) {
        sendHttpResponse(client_fd, HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request"));
        closeConnection(client_fd);
        return;
    }

    if (!validateRequest(method, body)) {
        sendHttpResponse(client_fd, HttpResponse::fromError(ErrorCode::Code::WEB_JSON_PARSE_ERROR,"Failed to parse HTTP request"));
        closeConnection(client_fd);
        return;
    }

    HttpResponse response = dispatchRequest(path, body);//这个应该是需要返回值出来

    sendHttpResponse(client_fd, response);

    closeConnection(client_fd);
}




bool WebService::receiveHttpRequest(int client_fd,std::string& request)
{
    constexpr size_t buffer_size = 8192;
    char buffer[buffer_size];

    ssize_t bytes = recv(client_fd, buffer, buffer_size - 1, 0);
    if (bytes <= 0)
        return false;

    buffer[bytes] = '\0';
    request.assign(buffer, bytes);

    return true;
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

void WebService::sendHttpResponse(int clientFd, const HttpResponse& response)
{
   std::ostringstream oss;

    oss << "HTTP/1.1 " << response.statusCode << " " << response.reasonPhrase << "\r\n";
    oss << "Content-Type: " << response.contentType << "\r\n";
    oss << "Content-Length: " << response.body.size() << "\r\n";

    // 默认连接关闭（你当前架构简单，先这样最稳）
    oss << "Connection: close\r\n";

    // 追加自定义头
    for (const auto& [key, value] : response.headers) {
        oss << key << ": " << value << "\r\n";
    }

    oss << "\r\n";

    // 先发 header
    std::string header = oss.str();
    send(clientFd, header.c_str(), header.size(), 0);


    // 再发 body（二进制安全）
    if (!response.body.empty()) {
        send(clientFd,reinterpret_cast<const char*>(response.body.data()),response.body.size(),0);
    }    
}



void WebService::closeConnection(int clientFd)
{
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
}


void WebService::publish(const std::string& topic,const std::string& payload){

}



void WebService::subscribe(const std::string& topic) {

}