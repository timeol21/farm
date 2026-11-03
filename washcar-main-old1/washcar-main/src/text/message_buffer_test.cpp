#include "business_layer/command/mqtt/message_buffer.h"
#include "common/sqlite/database_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

void testLogger(const std::string& msg) {
    std::cout << "[LOG] " << msg << std::endl;
}

int main() {
    std::cout << "========== MessageBuffer 测试程序 ==========" << std::endl;

    // 1. 初始化数据库
    DatabaseManager& dbManager = DatabaseManager::instance();
    if (!dbManager.init(".")) {
        std::cerr << "数据库初始化失败" << std::endl;
        return -1;
    }
    std::cout << "✓ 数据库初始化成功" << std::endl;

    // 2. 获取 MessageBuffer 单例
    MessageBuffer& buffer = MessageBuffer::getInstance();
    buffer.setLogger(testLogger);
    buffer.setDatabase(dbManager.getCommandDB());
    std::cout << "✓ MessageBuffer 初始化成功 (MAX_BUFFER_SIZE=" << MessageBuffer::MAX_BUFFER_SIZE << ")" << std::endl;

    // 3. 测试1：基本添加和获取
    std::cout << "\n--- 测试1: 基本添加和获取 ---" << std::endl;
    buffer.addMessage(MessageBuffer::MessageType::TYPE_TIMER_REPORT, "/topic/timer", "timer_report_1");
    buffer.addMessage(MessageBuffer::MessageType::TYPE_VALUE_CHANGE, "/topic/value", "value_change_1");
    buffer.addMessage(MessageBuffer::MessageType::TYPE_FAULT_REPORT, "/topic/fault", "fault_report_1");
    std::cout << "添加了3条消息，当前缓冲区大小: " << buffer.size() << std::endl;

    MessageBuffer::Message msg;
    if (buffer.getNext(msg)) {
        std::cout << "获取到消息: type=" << MessageBuffer::messageTypeToString(msg.type) 
                  << ", topic=" << msg.topic << ", payload=" << msg.payload << std::endl;
    }

    // 4. 测试2：自动去重
    std::cout << "\n--- 测试2: 自动去重 ---" << std::endl;
    buffer.clear();
    buffer.addMessage(MessageBuffer::MessageType::TYPE_WASHCAR_STATUS, "/washcar/status", "washcar_1");
    buffer.addMessage(MessageBuffer::MessageType::TYPE_WASHCAR_STATUS, "/washcar/status", "washcar_2");
    std::cout << "两次添加同类型消息，当前大小: " << buffer.size() << std::endl;
    
    if (buffer.getNext(msg)) {
        std::cout << "获取到的消息: topic=" << msg.topic << ", payload=" << msg.payload << std::endl;
    }

    // 5. 测试3：缓冲区满时保存到数据库
    std::cout << "\n--- 测试3: 缓冲区满时保存到数据库 ---" << std::endl;
    buffer.clear();
    
    // 添加 6 条不同类型的消息（缓冲区大小为5）
    const char* topics[] = {"/topic/timer", "/topic/value", "/topic/fault", "/washcar/status", "/washcar/result", "/solenoid/result"};
    MessageBuffer::MessageType types[] = {
        MessageBuffer::MessageType::TYPE_TIMER_REPORT,
        MessageBuffer::MessageType::TYPE_VALUE_CHANGE,
        MessageBuffer::MessageType::TYPE_FAULT_REPORT,
        MessageBuffer::MessageType::TYPE_WASHCAR_STATUS,
        MessageBuffer::MessageType::TYPE_WASHCAR_RESULT,
        MessageBuffer::MessageType::TYPE_SOLENOID_RESULT
    };
    
    for (int i = 0; i < 6; i++) {
        std::string payload = "test_message_" + std::to_string(i);
        buffer.addMessage(types[i], topics[i], payload);
        std::cout << "添加第" << (i+1) << "条 (type=" << MessageBuffer::messageTypeToString(types[i]) 
                  << ", topic=" << topics[i] << ") 后，缓冲区大小: " << buffer.size() << std::endl;
    }
    std::cout << "预期：添加第6条时，最旧的 timer_report 消息被保存到数据库" << std::endl;

    // 6. 测试4：从数据库恢复
    std::cout << "\n--- 测试4: 从数据库恢复 ---" << std::endl;
    buffer.clear();
    size_t loaded = buffer.loadFromDatabase(10);
    std::cout << "从数据库加载了 " << loaded << " 条消息" << std::endl;
    std::cout << "当前缓冲区大小: " << buffer.size() << std::endl;

    // 7. 测试5：按类型删除
    std::cout << "\n--- 测试5: 按类型删除 ---" << std::endl;
    buffer.clear();
    buffer.addMessage(MessageBuffer::MessageType::TYPE_TIMER_REPORT, "/topic/timer", "timer_2");
    buffer.addMessage(MessageBuffer::MessageType::TYPE_VALUE_CHANGE, "/topic/value", "value_1");
    std::cout << "添加后（自动去重）大小: " << buffer.size() << std::endl;
    
    size_t removed = buffer.removeByType(MessageBuffer::MessageType::TYPE_TIMER_REPORT);
    std::cout << "删除了 " << removed << " 条 TIMER_REPORT 消息" << std::endl;
    std::cout << "删除后大小: " << buffer.size() << std::endl;

    // 8. 测试6：多线程安全
    std::cout << "\n--- 测试6: 多线程安全 ---" << std::endl;
    buffer.clear();
    
    std::thread t1([&]() {
        for (int i = 0; i < 10; i++) {
            buffer.addMessage(MessageBuffer::MessageType::TYPE_SOLENOID_RESULT, 
                            "/solenoid/result", "solenoid_from_t1_" + std::to_string(i));
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 10; i++) {
            buffer.addMessage(MessageBuffer::MessageType::TYPE_WASHCAR_RESULT, 
                            "/washcar/result", "washcar_from_t2_" + std::to_string(i));
        }
    });

    t1.join();
    t2.join();
    std::cout << "多线程添加完成（自动去重后），当前缓冲区大小: " << buffer.size() << std::endl;

    // 9. 清理
    std::cout << "\n--- 清理过期消息 ---" << std::endl;
    buffer.cleanExpiredMessages();

    std::cout << "\n========== 测试完成 ==========" << std::endl;
    return 0;
}
