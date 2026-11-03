#pragma once
class MQTTCommandController;
class IController;
class IMqttProtocol;
class MessageBuffer;
#include "business_layer/command/mqtt/mqtt_object.h"

#include "business_layer/command/mqtt/network_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "common/config/config_manager.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mutex>
#include <functional>
class MQTTCommandController;
//优化的角度 1.加上消息队列，这样消息发送下来，不能出现堵塞的情况
         // 2.加上重连机制这样当连接断开时，能够自动尝试重新连接MQTT Broker
        //  3.加上心跳机制（简易的），定期发送心跳包以保持连接活跃，并检测连接状态

class IMqttProtocol;
class IController;

class MqttService : public NetworkService{
public:
    MqttService(const std::string& brokerIp, short port,
        const std::string& username, const std::string& password,
        const std::string& boxNo,
        IController& controller,IMqttProtocol& protocol)
        :  boxNo(boxNo),
           Ip(brokerIp),
           port(port),
           socketFd(-1),
           dispatcher(controller),
           m_protocol(protocol),
           messageBuffer_(MessageBuffer::getInstance()),
           connected_(false)
    {
        boxId = boxNo; // MQTT clientId 先按已验证可稳定连接的盒子号走
        m_protocol.setCredentials(username, password);
        running = false;
    }
    ~MqttService();
    
    bool start() override;

    void stop() override;

    void publish(const std::string& topic, const std::string& payload) override;
      
    void subscribe(const std::string& topic) override;

    // 业务上报：只传 topic 后半段（如 "event/post"），内部自动拼 flexem/boxNo/topicSuffix
    void publishMessage(const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType msgType);

    // 发布消息（带类型，用于消息缓冲系统）
    void publishWithBuffer(const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType msgType);

    // 处理连接断开事件
    void handleConnectionLost();

    // 处理连接恢复事件
    void handleReconnected();

    // 检查是否已连接
    bool isConnected() const;

    // ===== 仅供单元测试使用的注入点（生产代码不调用这三个）=====
    // 发送替身：非空时 sendMessage 直接走它，完全不碰 socket。
    // 用来模拟"发送成功 / 发送失败 / 可控的第 N 次失败"，测 outbox 的入库与补发路径。
    void setSendOverrideForTest(std::function<bool(const std::string&, const std::string&)> fn)
    { sendOverride_ = std::move(fn); }

    // 模拟连接断开 / 恢复，走的是真实代码读的同一个 connected_ 标志。
    void setConnectedForTest(bool connected) { connected_ = connected; }

    // 手动触发一次补发（真实路径由 run() 主循环调用）。
    void flushBufferedForTest(int maxSend) { sendBufferedMessages(maxSend); }
    
private:
    int processMqttLoop(long waitSec);

    bool reconnectWithRetry();

    void run();

    bool connectBroker();

    bool mqttHandshake();

    bool subscribeWashCarTopics();// 订阅洗车机主题

    MqttPacket receiveMessage(int socketFd,std::string& topic,std::string& payload);

    void closeConnection();

    void handlePacket(const MqttPacket& packet);

    bool sendPacket(const std::vector<uint8_t>& data);

    std::vector<uint8_t> recvPacket();
    // std::string buildPublishPacket(const std::string& topic,const std::string& payload);

    // void onMessage(const std::string& topic,const std::string& payload);
    
    // void subscribe(const std::string& topic);  //订阅主题
    
    // static void loadingSubscribe(int socketFd);

private:
    std::string boxId;

    std::string boxNo;

    std::string userId = "1";//
    
    std::string key;//加密需要使用key去加密，网页生成的key。

    std::string userName;

    std::string password;

    std::string Ip;

    short port;

    int socketFd;

    fd_set readfds_{};

    timeval tv_{};

    // 心跳间隔(秒),从 ConfigManager::mqtt().heartbeatSec 读
    int m_heartbeatSec = 30;

    std::thread mqttThread;

    std::atomic<bool> running{false};

    IController& dispatcher;

    IMqttProtocol& m_protocol;

    std::mutex sendMutex;

    // ================= 消息缓冲相关 =================
    
    // 连接状态（true=已连接，false=未连接）
    std::atomic<bool> connected_{false};

    // 消息缓冲引用
    MessageBuffer& messageBuffer_;

    // 测试注入点；生产路径恒为空，走真实 socket。
    std::function<bool(const std::string&, const std::string&)> sendOverride_;

    // 发送缓冲区中的消息
    void sendBufferedMessages(int maxSend = 2);

    // 拼装 flexem/boxNo/topicSuffix（业务层看不到完整 topic）
    std::string buildTopic(const std::string& topicSuffix) const;

    // 发送消息（内部方法，topicSuffix 内部补上 flexem/boxNo 后再发）
    bool sendMessage(const std::string& topicSuffix, const std::string& payload);

    // ================= 指数退避重连配置 =================

    // 重试基准间隔(秒),从 ConfigManager::mqtt().reconnectBaseSec 读
    int m_reconnectBaseSec = 2;

    // 最大重试次数,从 ConfigManager::mqtt().reconnectMaxRetry 读
    int m_reconnectMaxRetry = 5;

    // 计算指数退避延迟
    int calculateExponentialBackoff(int attempt);

    // 获取MQTT主题对应的消息类型
    MessageBuffer::MessageType getMessageTypeFromTopic(const std::string& topic);
};
