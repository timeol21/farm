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
#include "presentation_layer/http_command.h"
#include "business_layer/command/mqtt/network_service.h"
#include "common/config/config_load.h"
using nlohmann::json;


/**
 * @brief Web服务接口类
 * 提供基于TCP的HTTP JSON接口服务，处理客户端的JSON请求并返回JSON响应
 */
class WebService : public NetworkService{
public:
    /**
     * @brief 构造函数
 
     * @param ip 服务绑定的IP地址，默认为""

     *  @param port 服务监听的端口号，默认为8080

     * @param controller 处理HTTP请求的控制器实例引用 这里使用指针好还是引用好 请求下下发
     */
    WebService(IController& controller);

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
    


    void publish(const std::string& topic,const std::string& payload) override;
      
    void subscribe(const std::string& topic) override;


private:

    // ================= 生命周期步骤 =================

    bool initializeServerSocket(); // 初始化服务器套接字，绑定IP和端口，开始监听

    bool configureSocket(); //设置复用等选项

    bool  bindSocket(); // 绑定地址

    bool startListening(); // 开始监听

    void acceptLoop(); // 接受连接循环，处理客户端请求

    void handleClient(int client_fd); // 处理单个客户端请求

    // ================= HTTP处理步骤 =================

    bool receiveHttpRequest(int client_fd, std::string& request); // 接收完整HTTP请求，处理分包和超时

    bool parseHttpRequest(const std::string& raw,std::string& outMethod,std::string& outPath,std::string& outBody); // 从HTTP头部解析Content-Length

    bool validateRequest(const std::string& method, const std::string& body); // 验证HTTP方法和请求体格式

    HttpResponse dispatchRequest(const std::string& path, const std::string& body); // 根据路径分发到控制器处理

   void sendHttpResponse(int clientFd, const HttpResponse& response); //这个相应回去发送的

    void closeConnection(int clientFd);

private:
    IController& dispatcher_; // 控制器实例引用
    
    WebServiceConfig web_service_config;
    int m_server_fd{-1};
    std::thread acceptThread_;

    std::atomic<bool> running_;

    
};

#endif // WEB_SERVICE_H