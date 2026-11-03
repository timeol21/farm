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


bool MqttService::start() {
    // 启动时从 ConfigManager 读心跳/重连参数(必须在 ConfigManager::load() 之后调 start)
    const auto& m = ConfigManager::instance().mqtt();
    m_heartbeatSec    = m.heartbeatSec;
    m_reconnectBaseSec = m.reconnectBaseSec;
    m_reconnectMaxRetry = m.reconnectMaxRetry;

    if (running) {
        // 如果是 mqttThread 自己调过来的（重连场景），running 已经是 true
        bool inMqttThread = (std::this_thread::get_id() == mqttThread.get_id());

        if (inMqttThread) {
            closeConnection();
            connected_ = false;
            if (!connectBroker()) {
                // std::cerr << "MQTT connect failed\n";  // [2026-09-03] 改 LOG_ERROR 进 log 文件
        LOG_ERROR("[MqttService] MQTT connect failed");
                return false;
            }
            if (!mqttHandshake()) {
                // std::cerr << "MQTT handshake failed\n";  // [2026-09-03] 改 LOG_ERROR 进 log 文件
        LOG_ERROR("[MqttService] MQTT handshake failed");
                closeConnection();
                connected_ = false;
                return false;
            }

            subscribeWashCarTopics();
            
            // 连接成功
            connected_ = true;
            
            return true;
        }

        // std::cerr << "MQTT Service already running\n";  // [2026-09-03] 改 LOG_WARNING
        LOG_WARNING("[MqttService] already running");
        return false;
    }

    running = true;

    if (!connectBroker()) {
        // std::cerr << "MQTT connect failed\n";  // [2026-09-03] 改 LOG_ERROR 进 log 文件
        LOG_ERROR("[MqttService] MQTT connect failed");
        running = false;
        connected_ = false;
        return false;
    }
    if (!mqttHandshake()) {
        // std::cerr << "MQTT handshake failed\n";  // [2026-09-03] 改 LOG_ERROR 进 log 文件
        LOG_ERROR("[MqttService] MQTT handshake failed");
        running = false;
        connected_ = false;
        closeConnection();
        return false;
    }

    if (!subscribeWashCarTopics()) {
        // std::cerr << "MQTT subscribe failed\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] MQTT subscribe failed");
        running = false;
        connected_ = false;
        closeConnection();
        return false;
    }

    // 连接成功
    connected_ = true;
    
    // 连接恢复时处理未发送的消息
    handleReconnected();
    
    // 启动线程
    mqttThread = std::thread(&MqttService::run, this);
    return true;
}


void MqttService::stop() {
    if (running) {
        running = false;

        if (socketFd > 0) {
            auto packet = m_protocol.encodeDisconnect();
            sendPacket(packet);
        }
    }

    if (mqttThread.joinable()) {   // 这行提到 if(running) 外面
        mqttThread.join();
    }

    // 标记为未连接
    connected_ = false;
    
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

int MqttService::processMqttLoop(long waitSec) {
    FD_ZERO(&readfds_);
    FD_SET(socketFd, &readfds_);
    tv_.tv_sec = waitSec;
    tv_.tv_usec = 0;

    int ret = select(socketFd + 1, &readfds_, nullptr, nullptr, &tv_);

    if (ret > 0 && FD_ISSET(socketFd, &readfds_)) {
        std::string topic, payload;
        MqttPacket packet = receiveMessage(socketFd, topic, payload);
        if (packet.type != MqttPacketType::UNKNOWN) {
            handlePacket(packet);
            return 1;
        } else {
            // std::cerr << "recv failed, connection lost\n";  // [2026-09-03] 改 LOG_ERROR (用户追查 mqtt 重连关键 log)
        //LOG_ERROR("[MqttService] recv failed, connection lost");
            //return -1;
            // 服务器不会返回接受响应
        }
    }
    else if (ret == 0) {
        return 0;   // 超时，正常
    }
    else if (errno != EINTR) {
        // std::cerr << "select error: " << strerror(errno) << "\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR(std::string("[MqttService] select error: ") + strerror(errno));
        return -1;
    }

    return 1;
}

bool MqttService::subscribeWashCarTopics() {
    static uint16_t packetId = 1;
    const std::string topics[] = {
        "flexem/" + boxNo + "/" + FLEXEM_COMMAND_TOPIC,    // command
        "flexem/" + boxNo + "/" + FLEXEM_READ_TOPIC,       // read
        "flexem/" + boxNo + "/" + FLEXEM_REBOOT_TOPIC      // reboot
    };

    bool ok = true;
    for (const auto& topic : topics) {
        ok = sendPacket(m_protocol.encodeSubscribe(topic, packetId++)) && ok;
    }

    return ok;
}


bool MqttService::reconnectWithRetry() {
    int attempt = 0;
    while (running) {
        // 计算指数退避延迟：2, 4, 8, 16, 32 秒
        int delay = calculateExponentialBackoff(attempt);
        // std::cout << "Reconnect attempt " << (attempt + 1) << " (waiting " << delay << " seconds)" << std::endl;  // [2026-09-03] 改 LOG_INFO
        LOG_INFO(std::string("[MqttService] Reconnect attempt ") + std::to_string(attempt + 1) + " (waiting " + std::to_string(delay) + " seconds)");

        std::this_thread::sleep_for(std::chrono::seconds(delay));

        if (start()) {   // 直接调 start，它在 mqttThread 内不会创建新线程
            // std::cout << "Reconnect success" << std::endl;  // [2026-09-03] 改 LOG_INFO
        LOG_INFO("[MqttService] Reconnect success");
            
            // 连接恢复
            connected_ = true;
            handleReconnected();
            
            return true;
        }

        // std::cerr << "Reconnect attempt " << (attempt + 1) << " failed" << std::endl;  // [2026-09-03] 改 LOG_WARNING
        LOG_WARNING(std::string("[MqttService] Reconnect attempt ") + std::to_string(attempt + 1) + " failed");
        attempt++;
        
        if (attempt >= m_reconnectMaxRetry) {
            attempt = m_reconnectMaxRetry - 1;
        }
    }
    return false;
}

// private
void MqttService::run() {
    // std::cout << "MQTT Service is running..." << std::endl;  // [2026-09-03] 改 LOG_INFO
        LOG_INFO("[MqttService] MQTT Service is running");

    auto lastPing = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        auto sincePing = std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count();
        long waitSec = m_heartbeatSec - sincePing;
        if (waitSec < 0) waitSec = 0;

        long selectTimeout = (waitSec > 0) ? waitSec / 4 : 1;
        if (selectTimeout < 1) selectTimeout = 1;

        // 发送缓冲区中的消息
        sendBufferedMessages(2);

        int result = processMqttLoop(selectTimeout);

        if (result == 0) {
            auto timeSinceLastPing = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastPing).count();
            if (timeSinceLastPing >= m_heartbeatSec) {
                // 超时 → 发心跳
                auto ping = m_protocol.encodePing();
                if (sendPacket(ping)) {
                    lastPing = std::chrono::steady_clock::now();
                    // std::cout << "PING sent\n";  // [2026-09-03] 改 LOG_DEBUG(30s 一次,INFO 会刷 log)
                        LOG_DEBUG("[MqttService] PING sent");
                } else {
                    // PING 失败 → 重连
                    // std::cerr << "PING send failed, reconnecting...\n";  // [2026-09-03] 改 LOG_ERROR
                        LOG_ERROR("[MqttService] PING send failed, reconnecting");
                    closeConnection();
                    if (!reconnectWithRetry()) {
                        break;
                    }
                    lastPing = std::chrono::steady_clock::now();
                }
            }
        }
        else if (result < 0) {
            // 连接断开 → 重连
            // std::cerr << "Connection lost, reconnecting...\n";  // [2026-09-03] 改 LOG_ERROR (用户追查 mqtt 重连关键 log)
                LOG_ERROR("[MqttService] Connection lost, reconnecting");
            closeConnection();
            if (!reconnectWithRetry()) {
                break;
            }
            lastPing = std::chrono::steady_clock::now();
        }
    }

    running = false;
    // std::cout << "MQTT Service stopped." << std::endl;  // [2026-09-03] 改 LOG_INFO
        LOG_INFO("[MqttService] MQTT Service stopped");
}




bool MqttService::connectBroker() {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        // std::cerr << "socket create failed\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] socket create failed");
        return false;
    }

    // ������ 1. 设置非阻塞
    fcntl(socketFd, F_SETFL, O_NONBLOCK);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, Ip.c_str(), &serverAddr.sin_addr) <= 0) {
        // std::cerr << "Invalid IP\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] Invalid IP");
        return false;
    }

    // ������ 2. 发起连接
    int ret = connect(socketFd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    if (ret == 0) {
        // 立即连接成功（很少见）
        // std::cout << "connect success immediately\n";  // [2026-09-03] 改 LOG_INFO
        LOG_INFO("[MqttService] connect success immediately");
    }
    else if (ret < 0) {
        if (errno != EINPROGRESS) {
            // std::cerr << "connect error immediately\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] connect error immediately");
            return false;
        }

        // ������ 3. 等待连接完成（带超时）
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(socketFd, &wfds);

        timeval tv{};
        tv.tv_sec = 3;  // ⏱️ 超时3秒

        ret = select(socketFd + 1, nullptr, &wfds, nullptr, &tv);

        if (ret == 0) {
            // std::cerr << "connect timeout\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] connect timeout");
            return false;
        }
        else if (ret < 0) {
            // std::cerr << "select error\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] select error on connect");
            return false;
        }

        // ������ 4. 检查是否真的成功
        int err;
        socklen_t len = sizeof(err);
        getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &err, &len);

        if (err != 0) {
            // std::cerr << "connect failed: " << strerror(err) << "\n";  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR(std::string("[MqttService] connect failed: ") + strerror(err));
            return false;
        }
        int flags = fcntl(socketFd, F_GETFL, 0);
        fcntl(socketFd, F_SETFL, flags & ~O_NONBLOCK);
    }

    // std::cout << "connect success\n";  // [2026-09-03] 改 LOG_INFO
        LOG_INFO("[MqttService] connect success");
    return true;
}
bool MqttService::mqttHandshake() {
    auto packet = m_protocol.encodeConnect(boxId);
    
    // 打印 CONNECT 包内容（十六进制）
    // std::cout << "Sending CONNECT packet (" << packet.size() << " bytes): ";  // [2026-09-03] 改 LOG_DEBUG(高频,不要 INFO)
        LOG_DEBUG(std::string("[MqttService] Sending CONNECT packet, size=") + std::to_string(packet.size()));
    // for (auto b : packet) printf("%02X ", b);  // [2026-09-03] 注释掉 (高频 hex dump,改 LOG_DEBUG)
        // for (auto b : packet) printf("%02X ", b);
    // std::cout << std::endl;  // [2026-09-03] 注释 (上一行的 printf 已被注释)
        // std::cout << std::endl;
    
    if (!sendPacket(packet)) {
        LOG_ERROR("[MqttService] sendPacket failed");
        return false;
    }
    
    auto resp = recvPacket();
    // std::cout << "Received packet size: " << resp.size() << std::endl;  // [2026-09-03] 改 LOG_DEBUG
        LOG_DEBUG(std::string("[MqttService] Received packet size: ") + std::to_string(resp.size()));
    if (resp.empty()) {
        // std::cerr << "recvPacket returned empty" << std::endl;  // [2026-09-03] 改 LOG_ERROR
        LOG_ERROR("[MqttService] recvPacket returned empty");
        return false;
    }
    
    // 打印收到的原始数据
    std::cout << "Received data: ";
    for (auto b : resp) printf("%02X ", b);
    // std::cout << std::endl;  // [2026-09-03] 注释 (上一行的 printf 已被注释)
        // std::cout << std::endl;
    
    bool ok = m_protocol.parseConnAck(resp);
    if (!ok) {
        std::cerr << "parseConnAck failed" << std::endl;
    }
    return ok;
}
// bool MqttService::mqttHandshake() {
//     auto packet = m_protocol.encodeConnect(boxId);
//     if (!sendPacket(packet)) return false;
//     auto resp = recvPacket();
//     return m_protocol.parseConnAck(resp);
// }
std::vector<uint8_t> MqttService::recvPacket() {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(socketFd, &rfds);
    
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    
    int ret = select(socketFd + 1, &rfds, nullptr, nullptr, &tv);
    if (ret == 0) {
        std::cerr << "recvPacket timeout" << std::endl;
        return {};
    } else if (ret < 0) {
        std::cerr << "select error in recvPacket: " << strerror(errno) << std::endl;
        return {};
    }
    
    uint8_t headerByte;
    ssize_t n = recv(socketFd, &headerByte, 1, MSG_WAITALL);
    if (n != 1) {
        std::cerr << "recv headerByte failed: n=" << n << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
        return {};
    }
    
    // 读取剩余长度（变长）
    int multiplier = 1;
    int remainingLength = 0;
    uint8_t encodedByte;
    std::vector<uint8_t> remainingBytes;
    do {
        if (recv(socketFd, &encodedByte, 1, MSG_WAITALL) != 1) {
            std::cerr << "recv remaining length byte failed" << std::endl;
            return {};
        }
        remainingBytes.push_back(encodedByte);
        remainingLength += (encodedByte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128*128*128) {
            std::cerr << "Malformed remaining length" << std::endl;
            return {};
        }
    } while (encodedByte & 128);
    
    std::vector<uint8_t> packet;
    packet.push_back(headerByte);
    packet.insert(packet.end(), remainingBytes.begin(), remainingBytes.end());
    
    if (remainingLength > 0) {
        std::vector<uint8_t> payload(remainingLength);
        if (recv(socketFd, payload.data(), remainingLength, MSG_WAITALL) != remainingLength) {
            std::cerr << "recv payload failed, expected " << remainingLength << " bytes" << std::endl;
            return {};
        }
        packet.insert(packet.end(), payload.begin(), payload.end());
    }
    return packet;
}
// std::vector<uint8_t> MqttService::recvPacket() {
//     uint8_t header[2];

//     // 1️⃣ 先读固定头
//     if (recv(socketFd, header, 2, MSG_WAITALL) != 2) {
//         return {};
//     }

//     uint8_t type = header[0];
//     uint8_t remaining = header[1]; // 简化（实际是变长）

//     std::vector<uint8_t> packet(2 + remaining);
//     packet[0] = header[0];
//     packet[1] = header[1];

//     // 2️⃣ 读剩余部分
//     if (recv(socketFd, packet.data() + 2, remaining, MSG_WAITALL) != remaining) {
//         return {};
//     }

//     return packet;
// }

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
    LOG_DEBUG(std::string("[MqttService] sendPacket success, sent ") + std::to_string(total) + " bytes");
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
            std::cout << "[MqttService] recv topic: " << packet.topic << std::endl;
            // std::cout << "[MqttService] payload: " << packet.payload << std::endl;  // [2026-09-03] 跟下面 LOG_INFO 重复,注释
        // std::cout << "[MqttService] payload: " << packet.payload << std::endl;
            LOG_INFO("[MqttService] mqtt有效载荷"+packet.payload);
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

// ================= 消息缓冲系统实现 =================

// 业务上报：内部用 publishWithBuffer 走缓冲
void MqttService::publishMessage(const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType msgType) {
    publishWithBuffer(topicSuffix, payload, msgType);
}

// 发布消息（带缓冲）
void MqttService::publishWithBuffer(const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType msgType) {
    LOG_DEBUG(std::string("[MqttService] publish topic=") + topicSuffix + ", bytes=" + std::to_string(payload.size()));
    // 1. 如果未连接，直接保存到数据库
    if (!connected_) {
        messageBuffer_.saveToDatabase(msgType, topicSuffix, payload, std::time(nullptr) * 1000);
        LOG_WARNING(std::string("[MqttService] publish deferred (not connected), topic=") + topicSuffix);
        return;
    }

    // 2. 如果缓冲区满，保存到数据库
    if (messageBuffer_.size() >= MessageBuffer::MAX_BUFFER_SIZE) {
        messageBuffer_.saveToDatabase(msgType, topicSuffix, payload, std::time(nullptr) * 1000);
        LOG_WARNING(std::string("[MqttService] publish deferred (buffer full), topic=") + topicSuffix);
        return;
    }

    // 3. 添加到缓冲区（自动去重）
    // messageBuffer_.addMessage(msgType, topicSuffix, payload);
    // [2026-08-26] 删 addMessage:之前 addMessage + 立即 sendMessage 是双通道,memory buffer 留 element
    //    会被 run() 主循环 sendBufferedMessages 再发 1 次,同一 JSON 发 2 次以上(已观察 {"message":"success",...} 出现 3 次)。
    //    业务 publish 现在只走 sendMessage 同步发;reconnect 后 loadFromDatabase 把 db 未发消息加载到 buffer,
    //    主循环 flush 那条路径仍然 work(disconnect / buffer-full 走 saveToDatabase)。
    // 4. 尝试发送
    // 走通的消息从未进过 outbox，没有行需要标记。
    // 旧实现在这里 getLastDbIdByType(msgType) + markAsSent，会把库里同类型
    // 另一条尚未发送的旧消息误标成已发，那条消息就此丢失。
    if (sendMessage(topicSuffix, payload)) {
        LOG_DEBUG(std::string("[MqttService] publish sent, topic=") + topicSuffix);
    } else {
        // socket 写失败时必须进入持久 outbox，不能只靠内存缓冲。
        messageBuffer_.saveToDatabase(msgType, topicSuffix, payload, std::time(nullptr) * 1000);
        LOG_WARNING(std::string("[MqttService] publish failed, will retry, topic=") + topicSuffix);
    }
}

// 处理连接断开
void MqttService::handleConnectionLost() {
    // 1. 异步记录日志
    std::thread([]() {
        LOG_ERROR("MqttService: Connection lost, handling disconnection");
    }).detach();

    // 2. 将所有缓冲消息保存到数据库
    MessageBuffer::Message msg;
    while (messageBuffer_.getNext(msg)) {
        messageBuffer_.saveToDatabase(msg.type, msg.topic, msg.payload, msg.timestamp);
    }

    // 3. 启动指数退避重连（注意：这会在新线程中执行，不阻塞当前线程）
    // 注意：这里不启动新线程，因为 reconnectWithRetry 内部会处理
    // 重连逻辑在 run() 循环中处理
}

// 处理连接恢复
void MqttService::handleReconnected() {
    // 1. 清理过期的数据库消息
    messageBuffer_.cleanExpiredMessages();

    // 2. 故障报文是完整状态表:断网期间每条都如实入库了,这里收敛成最新一条,
    //    旧的直接删除。放在重连这一步而不是入库时,是为了让"只留最新"这个口径
    //    留在业务侧 —— 万一云端要全量历史,去掉这一行即可,数据库层不用动。
    messageBuffer_.collapseUnsentByType(MessageBuffer::MessageType::TYPE_FAULT_REPORT);

    // 3. 从数据库加载未发送的消息（最多加载50条）
    size_t loaded = messageBuffer_.loadFromDatabase(50);

    if (loaded > 0) {
        LOG_INFO("MqttService: Loaded " + std::to_string(loaded) + " messages from database");
    }
}

// 检查是否已连接
bool MqttService::isConnected() const {
    return connected_;
}

// 拼装 flexem/boxNo/topicSuffix
std::string MqttService::buildTopic(const std::string& topicSuffix) const {
    return "flexem/" + boxNo + "/" + topicSuffix;
}

// 发送消息（内部方法，topicSuffix 内部补上 flexem/boxNo 后再发）
bool MqttService::sendMessage(const std::string& topicSuffix, const std::string& payload) {
    const std::string topic = buildTopic(topicSuffix);

    // 测试注入点：单测用它替换真实 socket 写，模拟发送成功/失败。
    // 生产路径 sendOverride_ 恒为空，行为与之前完全一致。
    if (sendOverride_) {
        return sendOverride_(topic, payload);
    }

    auto packet = m_protocol.encodePublish(topic, payload);

    // [2026-08-26] 跟接收消息的日志格式对称(原来 hex dump 噪声太大,改成跟 PUBLISH 接收一样打 payload)
    std::cout << "[MqttService] send topic: " << topic << std::endl;
    // std::cout << "[MqttService] payload: " << payload << std::endl;  // [2026-09-03] 跟下面 LOG_INFO 重复,注释
        // std::cout << "[MqttService] payload: " << payload << std::endl;
    LOG_INFO("[MqttService] mqtt发送载荷" + payload);

    return sendPacket(packet);
}

// 发送缓冲区中的消息
void MqttService::sendBufferedMessages(int maxSend) {
    int sent = 0;
    while (!messageBuffer_.isEmpty() && sent < maxSend) {
        MessageBuffer::Message msg;
        if (!messageBuffer_.getNext(msg)) {
            break;
        }

        // 使用消息中保存的原始 topic
        if (sendMessage(msg.topic, msg.payload)) {
            if (msg.fromDatabase) {
                if (msg.dbId > 0) {
                    messageBuffer_.markAsSent(msg.dbId);
                }
            }
            sent++;
        } else {
            // 发送失败，放回缓冲区
            messageBuffer_.saveToDatabase(msg.type, msg.topic, msg.payload, msg.timestamp);
            break; // 暂时停止发送，等下次心跳
        }
    }
}

// 计算指数退避延迟
int MqttService::calculateExponentialBackoff(int attempt) {
    // 2, 4, 8, 16, 32 秒
    return m_reconnectBaseSec * (1 << attempt);
}

// 获取MQTT主题对应的消息类型
MessageBuffer::MessageType MqttService::getMessageTypeFromTopic(const std::string& topic) {
    // 根据主题判断消息类型
    if (topic.find("monitor") != std::string::npos) {
        return MessageBuffer::MessageType::TYPE_TIMER_REPORT;
    }
    if (topic.find("event") != std::string::npos) {
        return MessageBuffer::MessageType::TYPE_FAULT_REPORT;
    }
    if (topic.find("property") != std::string::npos) {
        if (topic.find("washcar") != std::string::npos) {
            return MessageBuffer::MessageType::TYPE_WASHCAR_STATUS;
        }
        return MessageBuffer::MessageType::TYPE_VALUE_CHANGE;
    }
    return MessageBuffer::MessageType::TYPE_OTHER;
}
