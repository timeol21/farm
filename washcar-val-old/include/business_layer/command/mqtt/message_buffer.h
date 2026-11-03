#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>

class MessageBuffer {
public:
    static MessageBuffer& getInstance();
    MessageBuffer(const MessageBuffer&) = delete;
    MessageBuffer& operator=(const MessageBuffer&) = delete;
    MessageBuffer(MessageBuffer&&) = delete;
    MessageBuffer& operator=(MessageBuffer&&) = delete;

    static constexpr size_t MAX_BUFFER_SIZE = 5;

    enum class MessageType {
        TYPE_TIMER_REPORT,       // 定时上报（发泡剂/蜡水）
        TYPE_VALUE_CHANGE,       // 变化上报（设备状态变化）
        TYPE_FAULT_REPORT,       // 故障上报
        TYPE_WASHCAR_STATUS,     // 洗车状态上报
        TYPE_WASHCAR_RESULT,     // 洗车命令执行结果
        TYPE_SOLENOID_RESULT,    // 电磁阀操作结果
        TYPE_POINT_RESULT,       // 点位操作结果
        TYPE_ALARM_CLEAR_RESULT, // 清除故障结果
        TYPE_OTHER               // 其他消息
    };

    struct Message {
        MessageType type;
        std::string topic;
        std::string payload;
        int64_t timestamp;
        int64_t dbId;
        bool fromDatabase;

        Message() : type(MessageType::TYPE_OTHER), timestamp(0), dbId(0), fromDatabase(false) {}
        Message(MessageType t, const std::string& tp, const std::string& p, int64_t ts, bool fromDb = false)
            : type(t), topic(tp), payload(p), timestamp(ts), dbId(0), fromDatabase(fromDb) {}
    };

    // 添加消息（自动去重）
    bool addMessage(MessageType type, const std::string& topic, const std::string& payload);
    
    // 获取下一条消息
    bool getNext(Message& message);
    
    // 缓冲区状态
    bool isEmpty() const;
    size_t size() const;
    void clear();
    
    // 按类型删除
    size_t removeByType(MessageType type);

    // 数据库操作
    void saveToDatabase(MessageType type, const std::string& topic, const std::string& payload, int64_t timestamp);
    size_t loadFromDatabase(size_t maxCount = 100);
    void deleteFromDatabase(int64_t dbId);
    void cleanExpiredMessages();
    void markAsSent(int64_t dbId);
    // 同类型未发消息收敛到最新一条,其余删除。补发前调用,业务口径在调用方。
    void collapseUnsentByType(MessageType type);

    // 类型转换
    static std::string messageTypeToString(MessageType type);
    static MessageType stringToMessageType(const std::string& typeStr);

    // 依赖注入
    void setDatabase(void* db);
    void setLogger(std::function<void(const std::string&)> logger);

private:
    MessageBuffer();
    ~MessageBuffer();
    int64_t getCurrentTimestamp();
    bool isFull() const;
    void log(const std::string& level, const std::string& message);

    // 写 outbox 的真正实现，要求调用方已持有 mutex_。
    // mutex_ 是非递归的，已持锁的路径(如 addMessage 溢出)必须走这个，
    // 不能再调 saveToDatabase，否则同一线程二次加锁会永久卡死。
    void saveToDatabaseLocked(MessageType type, const std::string& topic,
                              const std::string& payload, int64_t timestamp);

    std::vector<Message> buffer_;
    std::unordered_map<MessageType, size_t> latestIndex_;
    mutable std::mutex mutex_;
    void* db_;
    std::function<void(const std::string&)> logger_;
};
