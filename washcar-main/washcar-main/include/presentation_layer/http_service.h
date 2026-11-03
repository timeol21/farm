#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include <string>
#include <thread>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <cstring>
#include <atomic>
#include <vector>
#include "presentation_layer/http_command.h"
#include "presentation_layer/http_request.h"
using nlohmann::json;


/**
 * @brief Web服务接口类
 * 提供基于TCP的HTTP JSON接口服务，处理客户端的JSON请求并返回JSON响应
 */
class WebService {
public:
    /**
     * @brief 构造函数
 
     * @param ip 服务绑定的IP地址，默认为""

     *  @param port 服务监听的端口号，默认为8080

     * @param controller 处理HTTP请求的控制器实例引用 这里使用指针好还是引用好 请求下下发
     */
    WebService(const std::string& ip,short port, IController& controller,
               const std::string& webRoot = "");

    /**
     * @brief 析构函数
     * 自动停止服务并释放资源
     */
    ~WebService();

    /**
     * @brief 启动Web服务
     * @return 启动成功返回true，失败返回false
     */
    bool start();

    /**
     * @brief 停止Web服务
     * 停止监听、关闭套接字、等待工作线程退出
     */
    void stop();


    bool isRunning() const;

// 禁用拷贝构造和赋值运算符（避免线程和套接字资源拷贝问题）
    WebService(const WebService&) = delete;
    WebService& operator=(const WebService&) = delete;

    // 禁用移动构造和赋值运算符（可选，根据实际需求）
    WebService(WebService&&) = delete;
    WebService& operator=(WebService&&) = delete;
    

private:

    // ================= 生命周期步骤 =================

    bool initializeServerSocket(); // 初始化服务器套接字，绑定IP和端口，开始监听

    bool configureSocket(); //设置复用等选项

    bool  bindSocket(); // 绑定地址

    bool startListening(); // 开始监听

    void acceptLoop(); // 接受连接循环，处理客户端请求

    void handleClient(int client_fd); // 处理单个客户端请求(异常边界在这一层)

    void handleClientImpl(int client_fd); // handleClient 的正常流程

    // ================= HTTP处理步骤 =================

    bool receiveHttpRequest(int client_fd, std::string& request); // 接收完整HTTP请求，处理分包和超时

    bool parseHttpRequest(const std::string& raw, HttpRequest& request); // 解析方法/路径/查询串/body/Cookie

    bool validateRequest(const std::string& method, const std::string& body); // 验证HTTP方法和请求体格式

    HttpResponse dispatchRequest(const HttpRequest& request); // 根据路径分发到控制器处理

    // 配置台静态页:只允许 webRoot 下的普通文件,拒绝 .. 和绝对路径
    bool serveStatic(int clientFd, const std::string& path);

    void sendHttpResponse(int clientFd, int statusCode, const std::string& body,
                          const std::string& contentType = "application/json; charset=utf-8",
                          bool allowCors = true,
                          const std::vector<std::string>& headers = {});

    void closeConnection(int clientFd);

private:
    std::string m_bind_ip;
    short m_port;
    std::string m_webRoot;
    int m_server_fd{-1};
    std::thread acceptThread_;

    // 当前活跃连接数。配置台和 PLC 采集、MQTT 收发在同一个进程里,
    // 每条连接一个 detached 线程又不设上限,几百条慢连接就能把线程占满,
    // 洗车控制跟着卡住。
    std::atomic<int> m_activeConnections{0};

    std::atomic<bool> running_;

    IController& dispatcher_; // 控制器实例引用
};

#endif // WEB_SERVICE_H