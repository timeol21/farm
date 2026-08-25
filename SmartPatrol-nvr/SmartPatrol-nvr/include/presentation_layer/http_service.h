#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include <string>
#include <thread>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include "icommand_dispatcher.h"
#include "job_scheduler.h"
#include "../../lib/json/json.hpp"
#include "task.h"
#include "nlohmann/json.hpp"
#include "http_file_uploader.h"
#include "FileUtils.h"
#include "logger.h"
using nlohmann::json;
// 前置声明（避免头文件循环依赖）
class HTTPCommandController;
class Httpconfig;

/**
 * @brief Web服务接口类
 * 提供基于TCP的HTTP JSON接口服务，处理客户端的JSON请求并返回JSON响应
 */
class WebService {
public:
    /**
     * @brief 构造函数
 
     * @param ICommandDispatcher 任务调度接口指针
     */
    WebService(const std::string& httpPath, ICommandDispatcher* dispatcher);

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

    // 禁用拷贝构造和赋值运算符（避免线程和套接字资源拷贝问题）
    WebService(const WebService&) = delete;
    WebService& operator=(const WebService&) = delete;

    // 禁用移动构造和赋值运算符（可选，根据实际需求）
    WebService(WebService&&) = delete;
    WebService& operator=(WebService&&) = delete;

private:
    /**
     * @brief 服务运行循环
     * 持续监听客户端连接，处理新连接的接收
     */
    void run();

    /**
     * @brief 处理单个客户端连接
     * @param client_fd 客户端套接字描述符
     */
    void handleClient(int client_fd, const char* client_ip, uint16_t client_port);

    std::string parseRequestBody(const std::string& request, size_t content_length, int client_fd);

    size_t parseContentLength(const std::string& request);
    

    void sendErrorResponse(int client_fd, int status_code, const std::string& message, const json& extra = json{});

    void sendSuccessResponse(int client_fd, const json& response_json);

    std::string getStatusText(int status_code);
    
    // 成员变量
    std::string m_bind_ip;
    ICommandDispatcher* dispatcher_;      // 控制器实例（处理业务逻辑）                        // 监听端口
    int m_server_fd = -1;                // 服务端套接字描述符（初始化为无效值）
    std::atomic<bool> m_running;          // 服务运行状态标志
    std::thread m_thread;                // 服务运行线程
    uint16_t m_port = 8080;
    
};

class JobScheduler;
/**
 * @brief Web控制器接口类
 * 处理JSON请求的业务逻辑，与WebService解耦
 */
class HTTPCommandController : public ICommandDispatcher{
public:
    
    explicit HTTPCommandController(JobScheduler& scheduler);

    
    /**
     * @brief 处理JSON请求
     * @param topic 处理请求
     * @return payload 响应数据
     */
    void onMessage(const std::string& topic, const std::string& payload) override;

private:
    JobScheduler& scheduler_;

    

    void handleGetRealImage(const nlohmann::json& j);

    void handleGetSensorData(const nlohmann::json& j);
    void handleGetAllDeviceStatus(const nlohmann::json& j);

    void handVideoHistory(const nlohmann::json& j);
    
    void handVideoHistoryFile(const nlohmann::json& j);


    // void
};


#endif // WEB_SERVICE_H