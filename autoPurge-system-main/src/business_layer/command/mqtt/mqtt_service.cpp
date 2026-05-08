#include "business_layer/command/mqtt/mqtt_service.h"
#include "common/log/log_manager.h"

#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/controller.h"
#include <fcntl.h>
#include <errno.h>
// public

MqttService::~MqttService(){

    stop();

}


bool MqttService::start(){
    // 已经在运行，直接返回 false
    if(running) {
        std::cerr << "MQTT Service already running\n";
        return false;
    }

    // 标记启动中
    running = true;
    
    // 连接失败 → 恢复状态 + 返回 false
    if(!connectBroker()) {
        std::cerr << "MQTT connect failed\n";
        std::cout << "MQTT Service is not started\n"; 
        running = false;  // 关键：失败要重置标志
        return false;     // 返回 false
    }

    // 连接成功，启动线程
    mqttThread = std::thread(&MqttService::run, this);

    return true;  // 关键：成功返回 true
}

void MqttService::stop(){  
    
    if(!running) return;

    running = false;

    if(socketFd  > 0){
        auto packet = m_protocol.encodeDisconnect();
        sendPacket(packet);
                     
    }

    if(mqttThread.joinable()){
        mqttThread.join();
    }

    closeConnection();
}

void MqttService::publish(const std::string& topic, const std::string& payload){
    auto packet = m_protocol.encodePublish(topic, payload);

    sendPacket(packet);
}

void MqttService::subscribe(const std::string& topic)
{
    static uint16_t packetId = 1;
    auto packet = m_protocol.encodeSubscribe(topic, packetId++);

    sendPacket(packet);
}


// private
void MqttService::run(){
    std::cout<<"MQTT Service is running..."<<std::endl;
    while(running){
        std::string topic,payload;
        MqttPacket packet = receiveMessage(socketFd,topic,payload);

        handlePacket(packet);
        

    }
    std::cout<<"MQTT Service stopped."<<std::endl;
}



bool MqttService::connectBroker() {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        std::cerr << "socket create failed\n";
        return false;
    }

    // 🔥 1. 设置非阻塞
    fcntl(socketFd, F_SETFL, O_NONBLOCK);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(mqtt_service_config.port);

    if (inet_pton(AF_INET, mqtt_service_config.broker_ip.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP\n";
        return false;
    }

    // 🔥 2. 发起连接
    int ret = connect(socketFd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    if (ret == 0) {
        // 立即连接成功（很少见）
        std::cout << "connect success immediately\n";
    }
    else if (ret < 0) {
        if (errno != EINPROGRESS) {
            std::cerr << "connect error immediately\n";
            return false;
        }

        // 🔥 3. 等待连接完成（带超时）
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(socketFd, &wfds);

        timeval tv{};
        tv.tv_sec = 3;  // ⏱️ 超时3秒

        ret = select(socketFd + 1, nullptr, &wfds, nullptr, &tv);

        if (ret == 0) {
            std::cerr << "connect timeout\n";
            return false;
        }
        else if (ret < 0) {
            std::cerr << "select error\n";
            return false;
        }

        // 🔥 4. 检查是否真的成功
        int err;
        socklen_t len = sizeof(err);
        getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &err, &len);

        if (err != 0) {
            std::cerr << "connect failed: " << strerror(err) << "\n";
            return false;
        }
    }

    std::cout << "connect success\n";
    return true;
}

bool MqttService::mqttHandshake() {
    auto packet = m_protocol.encodeConnect(mqtt_service_config.client_id);

    if (!sendPacket(packet)) return false;

    
    auto resp = recvPacket();

    return m_protocol.parseConnAck(resp);
}

std::vector<uint8_t> MqttService::recvPacket() {
    uint8_t header[2];

    // 1️⃣ 先读固定头
    if (recv(socketFd, header, 2, MSG_WAITALL) != 2) {
        return {};
    }

    uint8_t type = header[0];
    uint8_t remaining = header[1]; // 简化（实际是变长）

    std::vector<uint8_t> packet(2 + remaining);
    packet[0] = header[0];
    packet[1] = header[1];

    // 2️⃣ 读剩余部分
    if (recv(socketFd, packet.data() + 2, remaining, MSG_WAITALL) != remaining) {
        return {};
    }

    return packet;
}

bool MqttService::sendPacket(const std::vector<uint8_t>& data){
    std::lock_guard<std::mutex> lock(sendMutex);
    if(socketFd < 0) return false;

    size_t total = 0;

    while(total < data.size()){
        ssize_t n = send(socketFd, data.data() + total, data.size() - total, 0);

        if(n <= 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 需要等待可写（select/epoll）
            }   
            return false;
        }

        total += n;
    }

    return true;  // ✅ 只要循环发完就是成功
}


MqttPacket MqttService::receiveMessage(int socketFd,std::string& topic,std::string& payload)
{
    std::vector<uint8_t> buffer(2048);

    ssize_t n = recv(socketFd, buffer.data(), buffer.size(), 0);

    MqttPacket packet;

    if(n <=  0) return packet;

    buffer.resize(n);

    m_protocol.decode(buffer,packet);

    topic = packet.topic;

    payload = packet.payload;

    return packet;
}


void MqttService::handlePacket(const MqttPacket& packet){
    switch(packet.type){
        case MqttPacketType::CONNACK:
        {
            std::cout << "MQTT Connected!\n";
            // ✅ 在这里订阅所有主题
            subscribe( OPERATE_PLC_WITH_VERIFY_TOPIC);//电磁阀
            subscribe( UPDATE_CONFIG_TOPIC);//配置更新
            subscribe( GET_ALL_DEVICE_STATUS_TOPIC);   //设备的状态
            break;
        }
        case MqttPacketType::PUBLISH:
        {   
            // std::cout << "recv topic: " << packet.topic << std::endl;
            std::cout << "payload: " << packet.payload << std::endl;
            LOG_INFO("mqtt有效载荷"+packet.payload);
            dispatcher.handleMqtt(packet.topic, packet.payload);
            break;
        }

        case MqttPacketType::PINGRESP:
        {
           
            break;
        }

        case MqttPacketType::SUBACK:
        {
            break;
        }

        default:
            break;
    }
}

void MqttService::closeConnection(){
    
    if(socketFd > 0){

        close(socketFd);

        socketFd = -1;
    }
}